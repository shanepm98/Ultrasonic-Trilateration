/*! \file receiver_loop.h
 *
 * \brief Bring-up pitch-catch receiver: configure the ICU-20201 for triggered receive-only
 * (CH_MODE_TRIGGERED_RX_ONLY), then own the periodic trigger loop - firing both this sensor and
 * the sender's sensor over the shared INT1 line - and stream the measured direct-path distance
 * to the serial console.
 *
 * Call after icu_post's post_run() has confirmed the sensor is up (ch_group_init / ch_init /
 * chbsp_esp32_init / ch_group_start all done). receiver_run() does not return.
 */

#ifndef RECEIVER_LOOP_H_
#define RECEIVER_LOOP_H_

#include <invn/soniclib/soniclib.h>

#ifdef __cplusplus
extern "C" {
#endif

/*!
 * \brief Configure triggered RX-only mode, then trigger periodically and stream direct-path
 * distances to the console forever.
 *
 * \param grp  sensor group (already started via ch_group_start())
 * \param dev  the single sensor device
 */
void receiver_run(ch_group_t *grp, ch_dev_t *dev);

#ifdef __cplusplus
}
#endif

#endif /* RECEIVER_LOOP_H_ */
