/*! \file readout_loop.h
 *
 * \brief Raw I/Q dump variant of the pitch-catch receiver: configure the ICU-20201 for triggered
 * receive-only (CH_MODE_TRIGGERED_RX_ONLY) exactly like receiver_loop.c, but instead of streaming
 * computed distances on a fixed cadence, idle for an on-demand serial command, trigger a single
 * measurement, and dump its full raw I/Q trace as plain text - for offboard signal processing /
 * bench tuning. See ../../README.md ("Raw data readout").
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
 * \brief Configure triggered RX-only mode, then repeatedly wait for a line typed on the serial
 * console, trigger one measurement, and dump its raw I/Q trace to the console forever.
 *
 * \param grp  sensor group (already started via ch_group_start())
 * \param dev  the single sensor device
 */
void readout_run(ch_group_t *grp, ch_dev_t *dev);

#ifdef __cplusplus
}
#endif

#endif /* READOUT_LOOP_H_ */
