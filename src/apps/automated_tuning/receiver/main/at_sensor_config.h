/*! \file at_sensor_config.h
 *
 * \brief Owns the sensor device handle, all staged RPC-driven configuration state, the
 * per-opcode reconfigure logic (one function per at_opcode_t that touches sensor config), the
 * ESP-NOW snapshot push, and the trigger-gating state consumed by trigger_loop.c.
 *
 * This is the correctness-critical module: it's where at_protocol.h's opcode table actually
 * meets SonicLib's ch_ and icu_gpt_ calls, and where the reconfigure-vs-trigger race is closed
 * (see at_sensor_trigger_cycle_mutex()).
 */

#ifndef AT_SENSOR_CONFIG_H_
#define AT_SENSOR_CONFIG_H_

#include <stdbool.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include <invn/soniclib/soniclib.h>

#include "at_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

/*! \brief One-time setup: stores dev, clears staged state, creates the module's mutexes. Call
 * once, before starting the RPC task or the trigger loop. */
void at_sensor_config_init(ch_dev_t *dev);

/*! \brief Mutex trigger_loop.c must hold for the full duration of ch_group_trigger() plus its
 * response wait, so a reconfigure sequence (which also takes this mutex, in
 * at_cfg_meas_reset()) can never start while a measurement is in flight. */
SemaphoreHandle_t at_sensor_trigger_cycle_mutex(void);

/*! \brief True once ch_set_mode() has ever succeeded after boot, and false whenever a
 * reconfigure sequence (AT_OP_MEAS_RESET..AT_OP_SET_MODE) is in flight. The trigger loop must
 * gate ch_group_trigger() on this. */
bool at_sensor_ready_to_trigger(void);

/* ===================== Opcode handlers =====================
 *
 * All share one signature so rpc_dispatch.c's dispatch table needs no function-pointer casts.
 * Each handler casts `body` to its own at_call_*_t type internally; rpc_dispatch.c has already
 * validated body's length matches that opcode's expected size before calling. `resp` is a
 * caller-supplied buffer at least as large as the largest response payload
 * (at_resp_get_thresholds_t, 32 bytes); handlers that produce no response data set *resp_len = 0
 * and never touch resp. Return value becomes the AT_MSG_RESPONSE status byte. */

at_status_t at_cfg_hello(const uint8_t *body, uint8_t *resp, uint8_t *resp_len);
at_status_t at_cfg_meas_reset(const uint8_t *body, uint8_t *resp, uint8_t *resp_len);
at_status_t at_cfg_meas_init(const uint8_t *body, uint8_t *resp, uint8_t *resp_len);
at_status_t at_cfg_add_segment_tx(const uint8_t *body, uint8_t *resp, uint8_t *resp_len);
at_status_t at_cfg_add_segment_rx(const uint8_t *body, uint8_t *resp, uint8_t *resp_len);
at_status_t at_cfg_add_segment_count(const uint8_t *body, uint8_t *resp, uint8_t *resp_len);
at_status_t at_cfg_meas_write_config(const uint8_t *body, uint8_t *resp, uint8_t *resp_len);
at_status_t at_cfg_set_odr(const uint8_t *body, uint8_t *resp, uint8_t *resp_len);
at_status_t at_cfg_set_num_samples(const uint8_t *body, uint8_t *resp, uint8_t *resp_len);
at_status_t at_cfg_set_max_range_meas(const uint8_t *body, uint8_t *resp, uint8_t *resp_len);
at_status_t at_cfg_set_max_range(const uint8_t *body, uint8_t *resp, uint8_t *resp_len);
at_status_t at_cfg_set_thresholds(const uint8_t *body, uint8_t *resp, uint8_t *resp_len);
at_status_t at_cfg_get_thresholds(const uint8_t *body, uint8_t *resp, uint8_t *resp_len);
at_status_t at_cfg_gpt_algo_configure(const uint8_t *body, uint8_t *resp, uint8_t *resp_len);
at_status_t at_cfg_set_mode(const uint8_t *body, uint8_t *resp, uint8_t *resp_len);
at_status_t at_cfg_get_status(const uint8_t *body, uint8_t *resp, uint8_t *resp_len);

#ifdef __cplusplus
}
#endif

#endif /* AT_SENSOR_CONFIG_H_ */
