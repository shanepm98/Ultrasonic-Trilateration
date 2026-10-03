/*! \file readout_loop.h
 *
 * \brief IR-triggered pitch-catch receiver: configure the ICU-20201 for triggered receive-only
 * (CH_MODE_TRIGGERED_RX_ONLY) and the IR LED, then run a console menu with three modes: an IR
 * timing tuner (optional pre-burst + trigger-burst length), a raw I/Q readout (one dump per Enter,
 * for offboard processing), and distance readings (one RANGE line per Enter). Each trigger is an
 * IR burst that fires both this sensor and the sender's via each board's IR demodulator on INT1.
 * See ../../README.md.
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
 * \brief Configure triggered RX-only mode and the IR LED, then run the console mode menu forever.
 *
 * \param grp  sensor group (already started via ch_group_start())
 * \param dev  the single sensor device
 */
void readout_run(ch_group_t *grp, ch_dev_t *dev);

#ifdef __cplusplus
}
#endif

#endif /* READOUT_LOOP_H_ */
