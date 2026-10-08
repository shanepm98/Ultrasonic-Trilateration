/*! \file sensor_calibration.h
 *
 * \brief Locked-down ICU-20201 measurement config and bench-derived calibration data, shared by
 * every app.
 *
 * The PC_* values are the measurement config. Sender and receiver are independent ESP-IDF
 * projects/builds, so any value where their independent calculations must agree for the protocol
 * to work lives here as a single source of truth, instead of being hand-duplicated as matching
 * #defines in each app.
 *
 * sc_rx_thresholds is the receiver's 8-segment icu_gpt detection threshold table, fitted to
 * labelled I/Q recordings at known distances (0.5-5 m) by calibrate_thresholds.py - see
 * docs/threshold_calibration.md. It assumes the PC_ODR and PC_TX_PULSE_US below and
 * SC_RX_THRESHOLDS_RINGDOWN_SAMPLES; changing any of them means re-capturing and regenerating
 * the table.
 *
 * Usage: add sensor_calibration to the app's main/CMakeLists.txt REQUIRES, then
 *
 *     #include "sensor_calibration.h"
 *     icu_gpt_algo_configure(dev, CH_DEFAULT_MEAS_NUM, &rx_gpt_cfg, &sc_rx_thresholds);
 *
 * (the threshold table in place of an app's local rx_thresholds).
 */

#ifndef SENSOR_CALIBRATION_H_
#define SENSOR_CALIBRATION_H_

#include <invn/soniclib/soniclib.h>
#include <invn/soniclib/sensor_fw/icu_gpt/icu_gpt.h> /* ch_thresholds_t, as the apps include it */

/* Transmit burst. The sender's ch_meas_add_segment_tx() uses these directly. The receiver's
 * count segment (ch_meas_add_segment_count()) must be sized from PC_TX_PULSE_US too, per
 * AN-000175 ("Count Segments"): "the count segment cycle count should match the other sensor's
 * transmit cycle count." Each board converts us -> cycles itself via ch_usec_to_cycles() against
 * its own calibrated op-frequency, so this stays correct even though the two sensors' natural
 * frequencies differ slightly. */
#define PC_TX_PULSE_US    450u /* ~640 SMCLK counts: AN-000175 Table 5's minimum to fully excite the MEMS */
#define PC_TX_PULSE_WIDTH 4u   /* max drive (AN-000175 Table 6); disables a sensor power-saving feature */
#define PC_TX_PHASE       8u

/* f_op/4: ~15.5 mm/sample one-way. ch_set_max_range() clamps to the firmware's max sample count,
 * which at this ODR still covers ~5.3 m one-way. */
#define PC_ODR CH_ODR_FREQ_DIV_4 /* must match so both sensors interpret sample timing the same way */

#define PC_MAX_RANGE_MM 5000u /* one-way full-scale range, both sides */

#define SC_RX_THRESHOLDS_RINGDOWN_SAMPLES 20u /* receiver ringdown_cancel_samples during capture */

extern const ch_thresholds_t sc_rx_thresholds;

#endif /* SENSOR_CALIBRATION_H_ */
