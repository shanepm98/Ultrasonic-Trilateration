/*! \file sender_loop.h
 *
 * \brief Bring-up pitch-catch sender: configure the ICU-20201 for triggered transmit/receive
 * (CH_MODE_TRIGGERED_TX_RX) and idle, waiting to be fired by the receiver board over the shared
 * INT1 trigger line.
 *
 * Call after icu_post's post_run() has confirmed the sensor is up (ch_group_init / ch_init /
 * chbsp_esp32_init / ch_group_start all done). sender_run() does not return.
 */

#ifndef SENDER_LOOP_H_
#define SENDER_LOOP_H_

#include <invn/soniclib/soniclib.h>

#ifdef __cplusplus
extern "C" {
#endif

/*!
 * \brief Configure triggered TX/RX mode and idle forever, awaiting external triggers.
 *
 * \param grp  sensor group (already started via ch_group_start())
 * \param dev  the single sensor device
 */
void sender_run(ch_group_t *grp, ch_dev_t *dev);

#ifdef __cplusplus
}
#endif

#endif /* SENDER_LOOP_H_ */
