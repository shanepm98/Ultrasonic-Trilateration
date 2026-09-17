#include "rpc_dispatch.h"

#include <stdint.h>
#include <stdio.h> /* TEMPORARY DEBUG: snprintf for dbg_print()'s formatted messages */

#include "driver/uart.h" /* TEMPORARY DEBUG: uart_write_bytes() to emit literal 0x00 delimiters
                           * around each debug line (see dbg_print() below) */
#include "esp_rom_sys.h" /* TEMPORARY DEBUG: esp_rom_printf - bypasses the ESP_LOGx silencing so
                           * we can see exactly how far a reconfigure sequence gets. Remove once
                           * the SET_MAX_RANGE hang is diagnosed. */

#include "at_protocol.h"
#include "at_sensor_config.h"
#include "serial_link.h"

/* TEMPORARY DEBUG: esp_rom_printf() has no frame delimiter of its own, so its text glues onto
 * whatever real COBS frame follows it with no 0x00 boundary in between - corrupting the host's
 * next parse. Wrapping each debug line in literal NUL bytes isolates it as its own (intentionally
 * unparseable) "frame" on the wire, so it can't corrupt the real response that follows. */
static void dbg_print(const char *msg) {
    static const uint8_t nul = 0;
    uart_write_bytes(UART_NUM_0, (const char *)&nul, 1);
    esp_rom_printf("%s", msg);
    uart_write_bytes(UART_NUM_0, (const char *)&nul, 1);
}

/* Largest real CALL body is at_call_gpt_algo_configure_t (40 bytes); largest response ret_data
 * is at_thresholds_t (32 bytes, AT_OP_GET_THRESHOLDS). 64/32 leave headroom without matching
 * serial_link.c's own limit exactly (they're independent buffers for independent purposes). */
#define RPC_MAX_BODY_LEN     64u
#define RPC_MAX_RET_DATA_LEN 32u

typedef at_status_t (*at_opcode_fn_t)(const uint8_t *body, uint8_t *resp, uint8_t *resp_len);

typedef struct {
    uint8_t        expected_len;
    at_opcode_fn_t fn;
} at_opcode_entry_t;

/* Indexed by opcode; AT_OP_RESERVED (0x00) and any gap is left zero-initialized (fn == NULL),
 * which dispatch() below treats as AT_STATUS_UNKNOWN_OPCODE. */
static const at_opcode_entry_t k_opcodes[AT_OP_GET_STATUS + 1] = {
    [AT_OP_HELLO]             = { 0,                                          at_cfg_hello },
    [AT_OP_MEAS_RESET]        = { sizeof(at_call_meas_reset_t),               at_cfg_meas_reset },
    [AT_OP_MEAS_INIT]         = { sizeof(at_call_meas_init_t),                at_cfg_meas_init },
    [AT_OP_ADD_SEGMENT_TX]    = { sizeof(at_call_add_segment_tx_t),           at_cfg_add_segment_tx },
    [AT_OP_ADD_SEGMENT_RX]    = { sizeof(at_call_add_segment_rx_t),           at_cfg_add_segment_rx },
    [AT_OP_ADD_SEGMENT_COUNT] = { sizeof(at_call_add_segment_count_t),        at_cfg_add_segment_count },
    [AT_OP_MEAS_WRITE_CONFIG] = { 0,                                          at_cfg_meas_write_config },
    [AT_OP_SET_ODR]           = { sizeof(at_call_set_odr_t),                  at_cfg_set_odr },
    [AT_OP_SET_NUM_SAMPLES]   = { sizeof(at_call_set_num_samples_t),          at_cfg_set_num_samples },
    [AT_OP_SET_MAX_RANGE_MEAS] = { sizeof(at_call_set_max_range_meas_t),      at_cfg_set_max_range_meas },
    [AT_OP_SET_MAX_RANGE]     = { sizeof(at_call_set_max_range_t),            at_cfg_set_max_range },
    [AT_OP_SET_THRESHOLDS]    = { sizeof(at_call_set_thresholds_t),           at_cfg_set_thresholds },
    [AT_OP_GET_THRESHOLDS]    = { 0,                                          at_cfg_get_thresholds },
    [AT_OP_GPT_ALGO_CONFIGURE] = { sizeof(at_call_gpt_algo_configure_t),      at_cfg_gpt_algo_configure },
    [AT_OP_SET_MODE]          = { sizeof(at_call_set_mode_t),                 at_cfg_set_mode },
    [AT_OP_GET_STATUS]        = { 0,                                          at_cfg_get_status },
};

static at_status_t dispatch(uint8_t opcode, const uint8_t *body, uint8_t body_len, uint8_t *resp,
                             uint8_t *resp_len) {
    if (opcode > AT_OP_GET_STATUS || k_opcodes[opcode].fn == NULL) {
        *resp_len = 0;
        return AT_STATUS_UNKNOWN_OPCODE;
    }
    if (body_len != k_opcodes[opcode].expected_len) {
        *resp_len = 0;
        return AT_STATUS_MALFORMED_ARGS;
    }
    return k_opcodes[opcode].fn(body, resp, resp_len);
}

void at_rpc_task(void *arg) {
    (void)arg;

    for (;;) {
        at_msg_header_t hdr;
        uint8_t         body[RPC_MAX_BODY_LEN];
        serial_link_recv_call(&hdr, body, sizeof(body));

        /* TEMPORARY DEBUG */
        char dbg_buf[64];
        snprintf(dbg_buf, sizeof(dbg_buf), "[dbg] opcode=0x%02x len=%u seq=%u enter\n", hdr.opcode, hdr.len,
                  hdr.seq);
        dbg_print(dbg_buf);

        uint8_t resp_body[1 + RPC_MAX_RET_DATA_LEN];
        uint8_t ret_len = 0;
        at_status_t status = dispatch(hdr.opcode, body, hdr.len, resp_body + 1, &ret_len);

        /* TEMPORARY DEBUG */
        snprintf(dbg_buf, sizeof(dbg_buf), "[dbg] opcode=0x%02x status=%u exit\n", hdr.opcode, (unsigned)status);
        dbg_print(dbg_buf);

        resp_body[0] = (uint8_t)status;
        serial_link_send(AT_MSG_RESPONSE, hdr.seq, hdr.opcode, resp_body, (uint8_t)(1u + ret_len));
    }
}
