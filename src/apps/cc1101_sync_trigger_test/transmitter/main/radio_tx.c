/*! \file radio_tx.c
 *
 * \brief Implementation of the async transmitter. See radio_tx.h.
 *
 * The RMT bytes encoder turns each data bit into one symbol of two equal halves at the same level,
 * so the output is plain NRZ at 0.1 us resolution. GDO0 idles low between transmissions, so the
 * carrier sits at the "0" symbol (ASK low amplitude / FSK low frequency) while the CC1101 stays in TX.
 */

#include "radio_tx.h"

#include "driver/rmt_tx.h"

#include "cc1101.h"
#include "cc_radio.h"

#define RADIO_TX_RMT_RESOLUTION_HZ (10 * 1000 * 1000) /* 1 tick = 0.1 us */
#define RADIO_TX_RMT_MEM_SYMBOLS   128                /* two RMT memory blocks - fewer refill interrupts */
#define RADIO_TX_SETTLE_US         2000               /* IDLE -> TX incl. autocal is ~800 us */

static rmt_channel_handle_t rmt_chan;
static rmt_encoder_handle_t rmt_encoder;
static uint32_t             rmt_baud;

static rmt_bytes_encoder_config_t encoder_config(uint32_t baud) {
	uint16_t half = (uint16_t)((RADIO_TX_RMT_RESOLUTION_HZ / baud + 1) / 2); /* half a bit, rounded */
	rmt_bytes_encoder_config_t cfg = {
			.bit0 = {.level0 = 0, .duration0 = half, .level1 = 0, .duration1 = half},
			.bit1 = {.level0 = 1, .duration0 = half, .level1 = 1, .duration1 = half},
			.flags.msb_first = 1,
	};
	return cfg;
}

esp_err_t radio_tx_init(uint32_t baud) {
	rmt_tx_channel_config_t chan_cfg = {
			.gpio_num          = CC_RADIO_GDO0_GPIO,
			.clk_src           = RMT_CLK_SRC_DEFAULT,
			.resolution_hz     = RADIO_TX_RMT_RESOLUTION_HZ,
			.mem_block_symbols = RADIO_TX_RMT_MEM_SYMBOLS,
			.trans_queue_depth = 1,
	};
	esp_err_t err = rmt_new_tx_channel(&chan_cfg, &rmt_chan);
	if (err != ESP_OK) return err;

	rmt_bytes_encoder_config_t enc_cfg = encoder_config(baud);
	err = rmt_new_bytes_encoder(&enc_cfg, &rmt_encoder);
	if (err != ESP_OK) return err;
	rmt_baud = baud;

	return rmt_enable(rmt_chan);
}

esp_err_t radio_tx_set_baud(uint32_t baud) {
	rmt_bytes_encoder_config_t enc_cfg = encoder_config(baud);
	esp_err_t err = rmt_bytes_encoder_update_config(rmt_encoder, &enc_cfg);
	if (err == ESP_OK) rmt_baud = baud;
	return err;
}

esp_err_t radio_tx_carrier_on(void) {
	esp_err_t err = cc1101_strobe(cc_radio_dev(), CC1101_STX);
	if (err != ESP_OK) return err;
	return cc1101_wait_marcstate(cc_radio_dev(), CC1101_MARCSTATE_TX, RADIO_TX_SETTLE_US);
}

esp_err_t radio_tx_carrier_off(void) {
	esp_err_t err = cc1101_strobe(cc_radio_dev(), CC1101_SIDLE);
	if (err != ESP_OK) return err;
	/* Wait until the radio has actually left TX: an STX strobed while it is still on its way to
	 * IDLE is ignored, which left the carrier off after every second register write. */
	return cc1101_wait_marcstate(cc_radio_dev(), CC1101_MARCSTATE_IDLE, RADIO_TX_SETTLE_US);
}

esp_err_t radio_tx_send(const uint8_t *data, size_t len) {
	rmt_transmit_config_t tx_cfg = {
			.loop_count      = 0,
			.flags.eot_level = 0, /* back to the "0" amplitude once the data ends */
	};
	esp_err_t err = rmt_transmit(rmt_chan, rmt_encoder, data, len, &tx_cfg);
	if (err != ESP_OK) return err;
	uint32_t timeout_ms = (uint32_t)((uint64_t)len * 8 * 1000 / rmt_baud) + 100;
	return rmt_tx_wait_all_done(rmt_chan, (int)timeout_ms);
}
