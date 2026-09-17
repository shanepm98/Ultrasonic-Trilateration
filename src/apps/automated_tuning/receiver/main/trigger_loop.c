#include "trigger_loop.h"

#include <stdbool.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "driver/gpio.h"

#include "at_common.h"
#include "at_protocol.h"
#include "at_sensor_config.h"
#include "espnow_relay.h"
#include "rpc_dispatch.h"
#include "serial_link.h"

static const char *TAG = "at-trigger";

typedef struct {
    bool     have_target;
    uint32_t range_q5; /* ch_get_range() units: millimetres * 32 */
    uint16_t amplitude;
} at_sample_t;

static QueueHandle_t g_sample_queue; /* length 1, overwrite semantics - always the latest sample */

/* Called by SonicLib after each measurement. Runs in the BSP's bsp_int_task (task level), so
 * blocking SPI reads here are fine. Same shape as pitch_catch_mode_hardware_test's
 * rx_data_ready_cb. */
static void data_ready_cb(ch_group_t *grp, uint8_t io_index, ch_interrupt_type_t int_type) {
    if (int_type != CH_INTERRUPT_TYPE_DATA_RDY) {
        return;
    }

    ch_dev_t *dev   = ch_get_dev_ptr(grp, io_index);
    uint32_t  range = ch_get_range(dev, CH_RANGE_DIRECT); /* direct pitch-catch: separate objects */

    at_sample_t s = {
        .have_target = (range != CH_NO_TARGET),
        .range_q5    = range,
        .amplitude   = (range != CH_NO_TARGET) ? ch_get_amplitude(dev) : 0,
    };
    xQueueOverwrite(g_sample_queue, &s);
}

/* Transmitter's sensor INT2 (data-ready) line, physically routed to AT_SENDER_READY_GPIO on this
 * board - separate from this board's own local sensor INT2, which soniclib_esp32_bsp owns.
 * Polled immediately before each trigger, not interrupt-driven - same pattern as pitch-catch. */
static void sender_ready_gpio_init(void) {
    gpio_config_t ready_cfg = {
        .pin_bit_mask = 1ULL << AT_SENDER_READY_GPIO,
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&ready_cfg);
}

void at_trigger_run(ch_group_t *grp, ch_dev_t *dev) {
    ESP_LOGI(TAG, "starting automated-tuning receiver: RPC over USB serial, config relay over ESP-NOW");

    /* From here on, UART0 belongs to the framed RPC link, not the console - see serial_link.h.
     * esp_log_level_set() must run before serial_link_init() installs the UART driver. */
    esp_log_level_set("*", ESP_LOG_NONE);

    serial_link_init();
    at_sensor_config_init(dev);
    ESP_ERROR_CHECK(espnow_relay_init());

    g_sample_queue = xQueueCreate(1, sizeof(at_sample_t));
    if (g_sample_queue == NULL) {
        for (;;) {
            vTaskDelay(portMAX_DELAY);
        }
    }

    ch_io_int_callback_set(grp, data_ready_cb);
    sender_ready_gpio_init();

    xTaskCreate(at_rpc_task, "at_rpc", 4096, NULL, 5, NULL);

    SemaphoreHandle_t trigger_mutex        = at_sensor_trigger_cycle_mutex();
    const TickType_t  response_timeout     = pdMS_TO_TICKS(AT_RESPONSE_TIMEOUT_MS);
    uint32_t          n                    = 0;

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(AT_TRIGGER_INTERVAL_MS));

        if (!at_sensor_ready_to_trigger()) {
            continue; /* no config applied yet, or a reconfigure is in flight */
        }
        if (gpio_get_level(AT_SENDER_READY_GPIO) == 0) {
            continue; /* transmitter's previous measurement hasn't finished */
        }

        xSemaphoreTake(trigger_mutex, portMAX_DELAY);
        ch_group_trigger(grp); /* pulses this board's INT1; the shared net also fires the transmitter's sensor */
        at_sample_t s;
        bool        got = xQueueReceive(g_sample_queue, &s, response_timeout) == pdTRUE;
        xSemaphoreGive(trigger_mutex);

        if (got) {
            at_telemetry_t t = {
                .meas_seq    = n,
                .have_target = s.have_target ? 1u : 0u,
                .range_q5    = s.range_q5,
                .amplitude   = s.amplitude,
            };
            serial_link_send(AT_MSG_TELEMETRY, 0, AT_OPCODE_TELEMETRY, &t, sizeof(t));
        }
        /* else: timeout - skip, matching pitch_catch's behavior. No frame is fabricated for a
         * timed-out measurement; the host's own inter-frame timeout is its diagnostic signal. */
        n++;
    }
}
