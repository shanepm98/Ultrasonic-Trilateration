/*! \file sensor_calibration.h
 *
 * \brief Bench-derived calibration data for the ICU-20201 pitch-catch receivers.
 *
 * sc_rx_thresholds is the receiver's 8-segment icu_gpt detection threshold table, fitted to
 * labelled I/Q recordings at known distances (0.5-5 m) by calibrate_thresholds.py - see
 * docs/threshold_calibration.md. Its start_sample values and levels assume the capture conditions
 * below; a receiver configured differently (other ODR, TX burst or ringdown cancel) needs the
 * table regenerated.
 *
 * Usage, in place of an app's local rx_thresholds:
 *
 *     #include "sensor_calibration.h"
 *     icu_gpt_algo_configure(dev, CH_DEFAULT_MEAS_NUM, &rx_gpt_cfg, &sc_rx_thresholds);
 *
 * and add sensor_calibration to the app's main/CMakeLists.txt REQUIRES.
 */

#ifndef SENSOR_CALIBRATION_H_
#define SENSOR_CALIBRATION_H_

#include <invn/soniclib/soniclib.h>
#include <invn/soniclib/sensor_fw/icu_gpt/icu_gpt.h> /* ch_thresholds_t, as the apps include it */

#define SC_RX_THRESHOLDS_ODR               CH_ODR_FREQ_DIV_4 /* PC_ODR during capture (ODR 5) */
#define SC_RX_THRESHOLDS_TX_US             450u              /* sender PC_TX_PULSE_US during capture */
#define SC_RX_THRESHOLDS_RINGDOWN_SAMPLES  20u               /* ringdown_cancel_samples during capture */

extern const ch_thresholds_t sc_rx_thresholds;

#endif /* SENSOR_CALIBRATION_H_ */
