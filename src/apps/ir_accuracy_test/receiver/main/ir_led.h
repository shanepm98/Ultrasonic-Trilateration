/*! \file ir_led.h
 *
 * \brief IR trigger LED on IR_LED_GPIO: a 38 kHz, 50% duty-cycle carrier from the ESP32's LEDC
 * peripheral, gated on for IR_BURST_US per trigger. The burst is seen by this board's own IR
 * demodulator and the sender's, each of which pulls its sensor's INT1 low to start a measurement.
 * See ../../README.md and ir_common.h.
 */

#ifndef IR_LED_H_
#define IR_LED_H_

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/*!
 * \brief Configure the LEDC timer + channel for the IR carrier, with the LED off (duty 0).
 *
 * \return ESP_OK, or the first LEDC configuration error
 */
esp_err_t ir_led_init(void);

/*!
 * \brief Emit one IR_BURST_US burst of the IR_CARRIER_HZ carrier, then turn the LED off.
 * Busy-waits for the burst duration. Requires ir_led_init() first.
 */
void ir_led_burst(void);

#ifdef __cplusplus
}
#endif

#endif /* IR_LED_H_ */
