/*! \file ir_led.h
 *
 * \brief IR trigger LED on IR_LED_GPIO. The ESP32's RMT peripheral generates the whole trigger
 * waveform in hardware: an optional pre-burst (IR_CARRIER_HZ carrier on for pre_on_us, then off for
 * pre_off_us), followed by the trigger burst (carrier on for trigger_us). Exact to 1 us. The burst
 * is seen by this board's own IR demodulator and the sender's, each of which pulls its sensor's
 * INT1 low to start a measurement. See ../../README.md and ir_common.h.
 */

#ifndef IR_LED_H_
#define IR_LED_H_

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/*! \brief IR trigger waveform timing. All times in microseconds, each 1..IR_MAX_SEGMENT_US. */
typedef struct {
	bool     pre_enabled; /*!< emit the pre-burst before the trigger burst */
	uint32_t pre_on_us;   /*!< pre-burst carrier-on time (ignored unless pre_enabled) */
	uint32_t pre_off_us;  /*!< gap between pre-burst and trigger burst (ignored unless pre_enabled) */
	uint32_t trigger_us;  /*!< trigger burst carrier-on time */
} ir_timing_t;

/*! \brief Default timing: no pre-burst, IR_BURST_US trigger burst. */
ir_timing_t ir_timing_default(void);

/*! \brief Print a one-line summary of \a t to stdout (no trailing newline). */
void ir_timing_print(const ir_timing_t *t);

/*!
 * \brief Configure the RMT TX channel, the 38 kHz carrier, and the encoder, with the LED off.
 *
 * \return ESP_OK, or the first RMT configuration error
 */
esp_err_t ir_led_init(void);

/*!
 * \brief Emit one trigger waveform per \a t, blocking until it has finished. Requires
 * ir_led_init() first.
 *
 * \return ESP_OK, or the RMT transmit error
 */
esp_err_t ir_led_fire(const ir_timing_t *t);

#ifdef __cplusplus
}
#endif

#endif /* IR_LED_H_ */
