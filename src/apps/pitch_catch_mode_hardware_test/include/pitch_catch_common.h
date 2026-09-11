/*! \file pitch_catch_common.h
 *
 * \brief Values shared between the pitch-catch sender and receiver firmware. Both apps are
 * independent ESP-IDF projects/builds, so anything with a cross-board physical meaning - i.e.
 * a value where the sender's and receiver's independent calculations must agree for the
 * protocol to work - lives here as a single source of truth, instead of being hand-duplicated
 * as matching #defines in two places.
 *
 * Per-sensor bench-tunable calibration (RX gain/attenuation, detection thresholds, ringdown/
 * static-filter samples) is NOT here - those legitimately differ per physical sensor unit and
 * stay local to each app's own *_loop.c.
 */

#ifndef PITCH_CATCH_COMMON_H_
#define PITCH_CATCH_COMMON_H_

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

/* Receiver's trigger cadence, and how long it waits for its own data-ready after triggering. */
#define PC_TRIGGER_INTERVAL_MS 100u /* 10 Hz */
#define PC_RESPONSE_TIMEOUT_MS 60u  /* > worst-case ~32ms measurement time at 5m, < interval */

/* Sender-ready handshake: the sender's sensor INT2 (data-ready) line is physically routed to
 * this GPIO on the receiver board, separate from the receiver's own local sensor INT2 on GPIO4.
 * Only the receiver reads this pin (via driver/gpio.h directly, not the chbsp_* BSP API, which
 * is scoped to a board's own local sensor) - it's documented here as part of the shared wiring
 * contract between the two boards. */
#define PC_SENDER_READY_GPIO GPIO_NUM_33

#endif /* PITCH_CATCH_COMMON_H_ */
