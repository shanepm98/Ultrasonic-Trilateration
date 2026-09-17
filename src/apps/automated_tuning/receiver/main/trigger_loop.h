/*! \file trigger_loop.h
 *
 * \brief Automated-tuning receiver's top-level entry point: brings up the serial RPC link, the
 * ESP-NOW relay, and sensor-config state, starts the RPC task, then owns the periodic
 * shared-INT1 trigger loop - firing both this sensor and the transmitter's sensor - streaming
 * each measured distance to the host as an AT_MSG_TELEMETRY frame.
 *
 * Call after icu_post's post_run() has confirmed the sensor is up (ch_group_init / ch_init /
 * chbsp_esp32_init / ch_group_start all done). at_trigger_run() does not return.
 */

#ifndef TRIGGER_LOOP_H_
#define TRIGGER_LOOP_H_

#include <invn/soniclib/soniclib.h>

#ifdef __cplusplus
extern "C" {
#endif

/*!
 * \brief Set up the RPC/ESP-NOW/sensor-config subsystems, then trigger periodically and stream
 * telemetry forever.
 *
 * \param grp  sensor group (already started via ch_group_start())
 * \param dev  the single sensor device
 */
void at_trigger_run(ch_group_t *grp, ch_dev_t *dev);

#ifdef __cplusplus
}
#endif

#endif /* TRIGGER_LOOP_H_ */
