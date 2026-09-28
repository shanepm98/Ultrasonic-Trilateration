/*! \file readout_loop.h
 *
 * \brief IR-triggered pitch-catch receiver: configure the ICU-20201 for triggered receive-only
 * (CH_MODE_TRIGGERED_RX_ONLY), then idle for an on-demand serial command, fire an IR burst that
 * triggers both this sensor and the sender's (via each board's IR demodulator on INT1), and dump
 * the measurement's full raw I/Q trace as plain text - for offboard signal processing / bench
 * tuning. See ../../README.md.
 *
 * Call after icu_post's post_run() has confirmed the sensor is up (ch_group_init / ch_init /
 * chbsp_esp32_init / ch_group_start all done). readout_run() does not return.
 */

#ifndef READOUT_LOOP_H_
#define READOUT_LOOP_H_

#include <invn/soniclib/soniclib.h>

#ifdef __cplusplus
extern "C" {
#endif

/*!
 * \brief Configure triggered RX-only mode and the IR LED, then repeatedly wait for a line typed on
 * the serial console, fire one IR trigger burst, and dump the resulting raw I/Q trace forever.
 *
 * \param grp  sensor group (already started via ch_group_start())
 * \param dev  the single sensor device
 */
void readout_run(ch_group_t *grp, ch_dev_t *dev);

#ifdef __cplusplus
}
#endif

#endif /* READOUT_LOOP_H_ */
