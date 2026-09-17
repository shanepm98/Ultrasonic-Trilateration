#include "serial_link.h"

#include <string.h>

#include "driver/uart.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_log.h"

#include "cobs.h"

static const char *TAG = "at-serial";

/* Simple, safe default - matches idf.py monitor's own default, and this link's low data rate
 * (10 Hz telemetry + human-paced RPC calls) has no need to push for a higher baud. */
#define SL_UART_BAUD_RATE 115200u

/* Largest real CALL/RESPONSE body in at_protocol.h is at_call_gpt_algo_configure_t (40 bytes);
 * TELEMETRY (11 bytes) and the ESP-NOW-only at_config_snapshot_t never cross this link. 64 bytes
 * leaves headroom without over-allocating. */
#define SL_MAX_BODY_LEN 64u
#define SL_MAX_PLAIN_LEN (sizeof(at_msg_header_t) + SL_MAX_BODY_LEN + 2u) /* header + body + crc16 */
#define SL_MAX_ENCODED_LEN COBS_MAX_ENCODED_SIZE(SL_MAX_PLAIN_LEN)

/* ESP-IDF's UART driver requires an RX ring buffer comfortably larger than the hardware FIFO
 * (128 bytes on ESP32); a fixed 512 bytes is well above that and holds several frames' worth of
 * backlog even though this link only ever expects one frame outstanding at a time. */
#define SL_UART_RX_BUF_SIZE 512u

static SemaphoreHandle_t g_tx_mutex;

void serial_link_init(void) {
    const uart_config_t cfg = {
        .baud_rate = SL_UART_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    ESP_ERROR_CHECK(uart_param_config(UART_NUM_0, &cfg));
    /* UART_PIN_NO_CHANGE keeps UART0's existing pins (the board's USB-UART bridge), which is
     * exactly the physical link we're taking over from the console. */
    ESP_ERROR_CHECK(uart_set_pin(UART_NUM_0, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE,
                                  UART_PIN_NO_CHANGE));
    /* tx_buffer_size = 0 makes uart_write_bytes() block until transmitted - fine, this link is
     * already strictly synchronous/serialized via g_tx_mutex. */
    ESP_ERROR_CHECK(uart_driver_install(UART_NUM_0, SL_UART_RX_BUF_SIZE, 0, 0, NULL, 0));

    g_tx_mutex = xSemaphoreCreateMutex();
}

void serial_link_send(at_msg_type_t type, uint8_t seq, uint8_t opcode, const void *body, uint8_t body_len) {
    uint8_t plain[SL_MAX_PLAIN_LEN];

    if (body_len > SL_MAX_BODY_LEN) {
        /* Caller bug (a body too large for this link) - never seen with the current opcode
         * table, but fail loudly in a way that can't corrupt the wire. */
        ESP_LOGE(TAG, "send: body_len %u exceeds SL_MAX_BODY_LEN %u - dropping", body_len, SL_MAX_BODY_LEN);
        return;
    }

    at_msg_header_t hdr = { .msg_type = (uint8_t)type, .seq = seq, .opcode = opcode, .len = body_len };
    memcpy(plain, &hdr, sizeof(hdr));
    if (body_len > 0) {
        memcpy(plain + sizeof(hdr), body, body_len);
    }

    size_t   plain_len = sizeof(hdr) + body_len;
    uint16_t crc        = at_crc16(plain, plain_len);
    plain[plain_len]     = (uint8_t)(crc & 0xFFu);
    plain[plain_len + 1] = (uint8_t)((crc >> 8) & 0xFFu);
    plain_len += 2;

    uint8_t encoded[SL_MAX_ENCODED_LEN];
    size_t  encoded_len = cobs_encode(plain, plain_len, encoded);

    xSemaphoreTake(g_tx_mutex, portMAX_DELAY);
    uart_write_bytes(UART_NUM_0, (const char *)encoded, encoded_len);
    const uint8_t delimiter = 0x00;
    uart_write_bytes(UART_NUM_0, (const char *)&delimiter, 1);
    xSemaphoreGive(g_tx_mutex);
}

/* Reads one byte at a time until a 0x00 delimiter is found, filling raw_buf (the COBS-encoded
 * bytes, excluding the delimiter itself). Blocks indefinitely - this is the only reader of
 * UART0, so there's never a "nothing more will ever arrive" case to time out on. Returns the
 * number of bytes accumulated, or 0 if raw_buf_cap was exceeded before a delimiter appeared (the
 * partial, over-long frame is discarded and reading resumes from the next byte). */
static size_t read_one_encoded_frame(uint8_t *raw_buf, size_t raw_buf_cap) {
    size_t n = 0;
    for (;;) {
        uint8_t b;
        int read = uart_read_bytes(UART_NUM_0, &b, 1, portMAX_DELAY);
        if (read != 1) {
            continue; /* spurious wakeup with no data; keep waiting */
        }
        if (b == 0x00) {
            return n;
        }
        if (n < raw_buf_cap) {
            raw_buf[n++] = b;
        } else {
            /* Over-long frame (garbage, or a peer bug) - drop what we have and keep consuming
             * bytes until the next delimiter, which the caller of this whole function will treat
             * as an empty/malformed frame and retry. */
            n = 0;
        }
    }
}

void serial_link_recv_call(at_msg_header_t *hdr_out, uint8_t *body_buf, uint8_t body_buf_cap) {
    uint8_t raw[SL_MAX_ENCODED_LEN];
    uint8_t plain[SL_MAX_PLAIN_LEN];

    for (;;) {
        size_t raw_len = read_one_encoded_frame(raw, sizeof(raw));
        if (raw_len == 0) {
            continue; /* empty or over-long frame */
        }

        bool   ok        = false;
        size_t plain_len = cobs_decode(raw, raw_len, plain, &ok);
        if (!ok || plain_len < sizeof(at_msg_header_t) + 2) {
            ESP_LOGW(TAG, "dropping malformed frame (cobs)");
            continue;
        }

        at_msg_header_t hdr;
        memcpy(&hdr, plain, sizeof(hdr));

        if (plain_len != sizeof(hdr) + hdr.len + 2u) {
            ESP_LOGW(TAG, "dropping frame with inconsistent length (header says %u, frame has %zu)", hdr.len,
                     plain_len);
            continue;
        }

        uint16_t expected_crc = at_crc16(plain, sizeof(hdr) + hdr.len);
        uint16_t got_crc = (uint16_t)plain[sizeof(hdr) + hdr.len] | ((uint16_t)plain[sizeof(hdr) + hdr.len + 1] << 8);
        if (expected_crc != got_crc) {
            ESP_LOGW(TAG, "dropping frame with bad crc16 (expected %04x, got %04x)", expected_crc, got_crc);
            continue;
        }

        if (hdr.msg_type != AT_MSG_CALL) {
            continue; /* this link only expects CALL frames from the host; ignore anything else */
        }

        if (hdr.len > body_buf_cap) {
            ESP_LOGW(TAG, "dropping CALL with body_len %u exceeding caller's buffer (%u)", hdr.len, body_buf_cap);
            continue;
        }

        memcpy(body_buf, plain + sizeof(hdr), hdr.len);
        *hdr_out = hdr;
        return;
    }
}
