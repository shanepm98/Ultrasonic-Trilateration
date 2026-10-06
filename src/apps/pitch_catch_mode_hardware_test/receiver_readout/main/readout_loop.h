/*! \file readout_loop.h
 *
 * \brief Raw I/Q dump variant of the pitch-catch receiver: configure the ICU-20201 for triggered
 * receive-only (CH_MODE_TRIGGERED_RX_ONLY) exactly like receiver_loop.c, but instead of streaming
 * computed distances on a fixed cadence, prompt on the serial console for a labelled batch (title,
 * actual distance, reading count) and dump each reading's full raw I/Q trace as a BEGIN/END block -
 * for offboard signal processing / bench tuning. See ../../README.md ("Raw data readout").
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
 * \brief Configure triggered RX-only mode, then forever: prompt for a batch (title, actual
 * distance, reading count), wait for Enter, and dump that many readings' raw I/Q traces.
 *
 * \param grp  sensor group (already started via ch_group_start())
 * \param dev  the single sensor device
 */
void readout_run(ch_group_t *grp, ch_dev_t *dev);

#ifdef __cplusplus
}
#endif

#endif /* READOUT_LOOP_H_ */
