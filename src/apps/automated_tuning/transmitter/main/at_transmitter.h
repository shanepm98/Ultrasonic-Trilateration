/*! \file at_transmitter.h
 *
 * \brief Automated-tuning transmitter's top-level entry point: brings up the ESP-NOW receive
 * link and a dedicated apply task that replays each incoming at_config_snapshot_t into the real
 * SonicLib calls, then idles - this board is always passively fired over the shared INT1 line,
 * never triggers itself, and never reads its own measurement results.
 *
 * Call after icu_post's post_run() has confirmed the sensor is up (ch_group_init / ch_init /
 * chbsp_esp32_init / ch_group_start all done). at_transmitter_run() does not return.
 */

#ifndef AT_TRANSMITTER_H_
#define AT_TRANSMITTER_H_

#include <invn/soniclib/soniclib.h>

#ifdef __cplusplus
extern "C" {
#endif

/*!
 * \param grp  sensor group (already started via ch_group_start()); unused beyond that - this
 *             app never registers a group-level data-ready callback
 * \param dev  the single sensor device
 */
void at_transmitter_run(ch_group_t *grp, ch_dev_t *dev);

#ifdef __cplusplus
}
#endif

#endif /* AT_TRANSMITTER_H_ */
