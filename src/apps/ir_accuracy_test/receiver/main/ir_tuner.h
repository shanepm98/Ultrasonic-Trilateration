/*! \file ir_tuner.h
 *
 * \brief Receiver mode 1, the interactive IR timing tuner. It asks whether to enable the pre-burst
 * and, if so, its on/off times, then the trigger-burst time. After that: Enter saves and returns,
 * Space fires a test trigger (repeatable, for the oscilloscope), and Backspace starts over.
 * See ../../README.md.
 */

#ifndef IR_TUNER_H_
#define IR_TUNER_H_

#include <stdbool.h>

#include "ir_led.h"

#ifdef __cplusplus
extern "C" {
#endif

/*!
 * \brief Fires one trigger with \a t and returns true if this board's sensor reported data-ready
 * (INT2) in time. Supplied by the caller, which owns the sensor plumbing.
 */
typedef bool (*ir_tuner_fire_fn)(const ir_timing_t *t);

/*!
 * \brief Run the tuner dialog. \a cfg is only overwritten when the user saves with Enter.
 * Requires console_init() and ir_led_init() first.
 */
void ir_tuner_run(ir_timing_t *cfg, ir_tuner_fire_fn fire);

#ifdef __cplusplus
}
#endif

#endif /* IR_TUNER_H_ */
