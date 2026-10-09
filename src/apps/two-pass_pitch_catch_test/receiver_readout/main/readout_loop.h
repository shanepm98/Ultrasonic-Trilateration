/*! \file readout_loop.h
 *
 * \brief Two-pass pitch-catch receiver, raw I/Q dump: configure the ICU-20201 for triggered
 * receive-only (CH_MODE_TRIGGERED_RX_ONLY) with two measurement slots - a coarse f_op/4 pass over
 * the full ~5 m, and a fine f_op/2 pass whose ~2.5 m window is padded to sit around the coarse
 * distance. Prompt on the serial console for a labelled batch (title, actual distance, reading
 * count) and, per reading, trigger both passes and dump each one's raw I/Q trace as a BEGIN/END
 * block - for offboard signal processing / bench tuning. See ../../README.md.
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
 * \brief Configure triggered RX-only mode (coarse + fine slots), then forever: prompt for a batch
 * (title, actual distance, reading count), wait for Enter, and dump that many coarse/fine pairs.
 *
 * \param grp  sensor group (already started via ch_group_start())
 * \param dev  the single sensor device
 */
void readout_run(ch_group_t *grp, ch_dev_t *dev);

#ifdef __cplusplus
}
#endif

#endif /* READOUT_LOOP_H_ */
