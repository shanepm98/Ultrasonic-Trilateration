/*! \file ir_led.c
 *
 * \brief Implementation of the IR trigger LED. See ir_led.h.
 *
 * The LED driver on IR_LED_GPIO is active-high (GPIO high = LED on; confirmed on hardware). RMT
 * symbols describe the waveform envelope at 1 us resolution (level 1 = carrier on), and the RMT
 * carrier modulator gates the 38 kHz carrier onto the high levels, so the pre-burst / gap / trigger
 * durations are exact rather than quantized to carrier periods. The output idles low (LED off).
 */

#include "ir_led.h"

#include <inttypes.h>
#include <stdio.h>

#include "driver/rmt_tx.h"

#include "ir_common.h" /* IR_LED_GPIO, IR_CARRIER_HZ, IR_BURST_US, IR_MAX_SEGMENT_US */

#define IR_RMT_RESOLUTION_HZ  (1000 * 1000) /* 1 tick = 1 us */
#define IR_RMT_MEM_SYMBOLS    64            /* one RMT memory block on the ESP32 */
#define IR_RMT_MAX_TICKS      32767u        /* rmt_symbol_word_t durations are 15 bits */
#define IR_RMT_DONE_TIMEOUT_MS 1000         /* > 3 x IR_MAX_SEGMENT_US */

/* Worst case: 3 segments of IR_MAX_SEGMENT_US, each split into IR_RMT_MAX_TICKS chunks. */
#define IR_MAX_CHUNKS  (3 * ((IR_MAX_SEGMENT_US + IR_RMT_MAX_TICKS - 1) / IR_RMT_MAX_TICKS))
#define IR_MAX_SYMBOLS ((IR_MAX_CHUNKS + 1) / 2)

static rmt_channel_handle_t ir_chan;
static rmt_encoder_handle_t ir_encoder;

ir_timing_t ir_timing_default(void) {
	ir_timing_t t = {
			.pre_enabled = false,
			.pre_on_us   = 0,
			.pre_off_us  = 0,
			.trigger_us  = IR_BURST_US,
	};
	return t;
}

void ir_timing_print(const ir_timing_t *t) {
	if (t->pre_enabled) {
		printf("pre-burst ON %" PRIu32 " us, OFF %" PRIu32 " us, trigger %" PRIu32 " us", t->pre_on_us,
		       t->pre_off_us, t->trigger_us);
	} else {
		printf("pre-burst disabled, trigger %" PRIu32 " us", t->trigger_us);
	}
}

esp_err_t ir_led_init(void) {
	rmt_tx_channel_config_t chan_cfg = {
			.gpio_num          = IR_LED_GPIO,
			.clk_src           = RMT_CLK_SRC_DEFAULT,
			.resolution_hz     = IR_RMT_RESOLUTION_HZ,
			.mem_block_symbols = IR_RMT_MEM_SYMBOLS,
			.trans_queue_depth = 1,
	};
	esp_err_t err = rmt_new_tx_channel(&chan_cfg, &ir_chan);
	if (err != ESP_OK) return err;

	rmt_carrier_config_t carrier_cfg = {
			.frequency_hz = IR_CARRIER_HZ,
			.duty_cycle   = 0.5f, /* 50% */
	};
	err = rmt_apply_carrier(ir_chan, &carrier_cfg);
	if (err != ESP_OK) return err;

	rmt_copy_encoder_config_t enc_cfg = {};
	err = rmt_new_copy_encoder(&enc_cfg, &ir_encoder);
	if (err != ESP_OK) return err;

	return rmt_enable(ir_chan);
}

esp_err_t ir_led_fire(const ir_timing_t *t) {
	/* Envelope as (level, duration) segments, each split into chunks that fit a 15-bit duration. */
	uint8_t  levels[IR_MAX_CHUNKS + 1];
	uint16_t durs[IR_MAX_CHUNKS + 1];
	size_t   n = 0;

	const uint8_t  seg_level[3] = {1, 0, 1};
	const uint32_t seg_us[3]    = {t->pre_enabled ? t->pre_on_us : 0, t->pre_enabled ? t->pre_off_us : 0,
	                               t->trigger_us};
	for (int s = 0; s < 3; s++) {
		uint32_t left = seg_us[s] > IR_MAX_SEGMENT_US ? IR_MAX_SEGMENT_US : seg_us[s];
		while (left > 0) {
			uint32_t chunk = left > IR_RMT_MAX_TICKS ? IR_RMT_MAX_TICKS : left;
			levels[n] = seg_level[s];
			durs[n]   = (uint16_t)chunk;
			n++;
			left -= chunk;
		}
	}
	if (n % 2) {
		/* Pad the last symbol's second half with 1 us of low: a zero duration would end the
		 * transmission early on the ESP32's RMT. */
		levels[n] = 0;
		durs[n]   = 1;
		n++;
	}

	rmt_symbol_word_t symbols[IR_MAX_SYMBOLS];
	size_t            num_symbols = n / 2;
	for (size_t i = 0; i < num_symbols; i++) {
		symbols[i].level0    = levels[2 * i];
		symbols[i].duration0 = durs[2 * i];
		symbols[i].level1    = levels[2 * i + 1];
		symbols[i].duration1 = durs[2 * i + 1];
	}

	rmt_transmit_config_t tx_cfg = {
			.loop_count = 0,
			.flags.eot_level = 0, /* LED off once the waveform ends */
	};
	esp_err_t err = rmt_transmit(ir_chan, ir_encoder, symbols, num_symbols * sizeof(rmt_symbol_word_t), &tx_cfg);
	if (err != ESP_OK) return err;
	return rmt_tx_wait_all_done(ir_chan, IR_RMT_DONE_TIMEOUT_MS);
}
