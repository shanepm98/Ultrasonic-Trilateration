/*! \file ir_common.h
 *
 * \brief Values shared between the IR-triggered pitch-catch sender and receiver firmware. Both
 * apps are independent ESP-IDF projects/builds, so anything with a cross-board physical meaning -
 * i.e. a value where the sender's and receiver's independent calculations must agree for the
 * protocol to work - lives here as a single source of truth, instead of being hand-duplicated as
 * matching #defines in two places.
 *
 * Derived from ../../pitch_catch_mode_hardware_test/include/pitch_catch_common.h; the PC_ sensor
 * values are kept identical (and identically named) so results stay directly comparable with the
 * hardwired-trigger test. The hardwired-only values (trigger cadence, GPIO33 sender-ready tap) are
 * gone - there are no board-to-board wires in this test.
 *
 * Per-sensor bench-tunable calibration (RX gain/attenuation, detection thresholds, ringdown/
 * static-filter samples) is NOT here - those legitimately differ per physical sensor unit and
 * stay local to each app's own *_loop.c.
 */

#ifndef IR_COMMON_H_
#define IR_COMMON_H_

#include <invn/soniclib/soniclib.h>
#include "driver/gpio.h"

/* Transmit burst. The sender's ch_meas_add_segment_tx() uses these directly. The receiver's
 * count segment (ch_meas_add_segment_count()) must be sized from PC_TX_PULSE_US too, per
 * AN-000175 ("Count Segments"): "the count segment cycle count should match the other sensor's
 * transmit cycle count." Each board converts us -> cycles itself via ch_usec_to_cycles() against
 * its own calibrated op-frequency, so this stays correct even though the two sensors' natural
 * frequencies differ slightly. */
#define PC_TX_PULSE_US    80u
#define PC_TX_PULSE_WIDTH 3u
#define PC_TX_PHASE       8u

#define PC_ODR CH_ODR_DEFAULT /* must match so both sensors interpret sample timing the same way */

#define PC_MAX_RANGE_MM 5000u /* one-way full-scale range, both sides */

/* IR trigger link. Every board has an IR LED on IR_LED_GPIO and a 38 kHz IR demodulator whose
 * (active-low) output drives the sensor's INT1 trigger pin directly. Only the receiver drives its
 * LED; its burst reaches its own demodulator and the sender's, so both sensors see the same
 * demodulator latency. IR_CARRIER_HZ must match the demodulator part's centre frequency. */
#define IR_LED_GPIO   GPIO_NUM_25
#define IR_CARRIER_HZ 38000u /* 50% duty-cycle carrier */
#define IR_BURST_US   5000u  /* carrier-on time per trigger */

/* How long the receiver waits for its own data-ready (INT2) after starting an IR burst. */
#define IR_RESPONSE_TIMEOUT_MS 500u

/* IR module supply: the IR module is powered from this GPIO, driven constantly high. */
#define IR_POWER_GPIO GPIO_NUM_26

/*!
 * \brief Drive IR_POWER_GPIO high to power the IR module, and leave it high forever.
 *
 * Call first thing in app_main(), before POST / any sensor I/O: an unpowered demodulator can clamp
 * the INT1 net low through its output protection diode, holding the sensor's trigger line active
 * while SonicLib programs the sensor. Powering it early also gives it time to settle.
 */
static inline esp_err_t ir_power_on(void) {
    gpio_config_t pwr_cfg = {
        .pin_bit_mask = 1ULL << IR_POWER_GPIO,
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&pwr_cfg);
    if (err != ESP_OK) return err;
    return gpio_set_level(IR_POWER_GPIO, 1);
}

#endif /* IR_COMMON_H_ */
