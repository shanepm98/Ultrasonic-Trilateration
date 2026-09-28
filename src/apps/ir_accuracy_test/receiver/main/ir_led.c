/*! \file ir_led.c
 *
 * \brief Implementation of the IR trigger LED. See ir_led.h.
 *
 * The LED driver on IR_LED_GPIO is active-high (GPIO high = LED on; confirmed on hardware), so
 * duty 0 is LED off between bursts.
 */

#include "ir_led.h"

#include "driver/ledc.h"
#include "esp_rom_sys.h"

#include "ir_common.h" /* IR_LED_GPIO, IR_CARRIER_HZ, IR_BURST_US */

#define IR_LEDC_MODE       LEDC_LOW_SPEED_MODE
#define IR_LEDC_TIMER      LEDC_TIMER_0
#define IR_LEDC_CHANNEL    LEDC_CHANNEL_0
#define IR_LEDC_RESOLUTION LEDC_TIMER_10_BIT /* 80 MHz APB / 38 kHz allows up to 11 bits */
#define IR_LEDC_DUTY_ON    (1u << (IR_LEDC_RESOLUTION - 1)) /* 50% */

esp_err_t ir_led_init(void) {
	ledc_timer_config_t timer_cfg = {
			.speed_mode      = IR_LEDC_MODE,
			.duty_resolution = IR_LEDC_RESOLUTION,
			.timer_num       = IR_LEDC_TIMER,
			.freq_hz         = IR_CARRIER_HZ,
			.clk_cfg         = LEDC_AUTO_CLK,
	};
	esp_err_t err = ledc_timer_config(&timer_cfg);
	if (err != ESP_OK) return err;

	ledc_channel_config_t chan_cfg = {
			.gpio_num   = IR_LED_GPIO,
			.speed_mode = IR_LEDC_MODE,
			.channel    = IR_LEDC_CHANNEL,
			.intr_type  = LEDC_INTR_DISABLE,
			.timer_sel  = IR_LEDC_TIMER,
			.duty       = 0, /* LED off until the first burst */
			.hpoint     = 0,
	};
	return ledc_channel_config(&chan_cfg);
}

void ir_led_burst(void) {
	/* A new duty takes effect at the next carrier period boundary, so burst start/end jitter by up
	 * to one period (~26 us) - irrelevant here, since both demodulators see the same light. */
	ledc_set_duty(IR_LEDC_MODE, IR_LEDC_CHANNEL, IR_LEDC_DUTY_ON);
	ledc_update_duty(IR_LEDC_MODE, IR_LEDC_CHANNEL);
	esp_rom_delay_us(IR_BURST_US);
	ledc_set_duty(IR_LEDC_MODE, IR_LEDC_CHANNEL, 0);
	ledc_update_duty(IR_LEDC_MODE, IR_LEDC_CHANNEL);
}
