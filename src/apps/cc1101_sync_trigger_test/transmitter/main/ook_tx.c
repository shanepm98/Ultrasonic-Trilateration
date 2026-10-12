/*! \file ook_tx.c
 *
 * \brief Implementation of the async-OOK transmitter. See ook_tx.h.
 *
 * The RMT bytes encoder turns each data bit into one symbol of two equal halves at the same level
 * (1 = carrier on), so the output is plain NRZ at 0.1 us resolution. GDO0 idles low between
 * transmissions, so the carrier is off whenever the CC1101 enters or leaves TX.
 */

#include "ook_tx.h"

#include "driver/rmt_tx.h"

#include "cc1101.h"
#include "cc_ook.h"

#define OOK_RMT_RESOLUTION_HZ (10 * 1000 * 1000) /* 1 tick = 0.1 us */
#define OOK_RMT_MEM_SYMBOLS   128                /* two RMT memory blocks - fewer refill interrupts */
#define OOK_TX_SETTLE_US      2000               /* IDLE -> TX incl. autocal is ~800 us */

static rmt_channel_handle_t ook_chan;
static rmt_encoder_handle_t ook_encoder;
static uint32_t             ook_baud;

static rmt_bytes_encoder_config_t encoder_config(uint32_t baud) {
	uint16_t half = (uint16_t)((OOK_RMT_RESOLUTION_HZ / baud + 1) / 2); /* half a bit, rounded */
	rmt_bytes_encoder_config_t cfg = {
			.bit0 = {.level0 = 0, .duration0 = half, .level1 = 0, .duration1 = half},
			.bit1 = {.level0 = 1, .duration0 = half, .level1 = 1, .duration1 = half},
			.flags.msb_first = 1,
	};
	return cfg;
}

esp_err_t ook_tx_init(uint32_t baud) {
	rmt_tx_channel_config_t chan_cfg = {
			.gpio_num          = CC_OOK_GDO0_GPIO,
			.clk_src           = RMT_CLK_SRC_DEFAULT,
			.resolution_hz     = OOK_RMT_RESOLUTION_HZ,
			.mem_block_symbols = OOK_RMT_MEM_SYMBOLS,
			.trans_queue_depth = 1,
	};
	esp_err_t err = rmt_new_tx_channel(&chan_cfg, &ook_chan);
	if (err != ESP_OK) return err;

	rmt_bytes_encoder_config_t enc_cfg = encoder_config(baud);
	err = rmt_new_bytes_encoder(&enc_cfg, &ook_encoder);
	if (err != ESP_OK) return err;
	ook_baud = baud;

	return rmt_enable(ook_chan);
}

esp_err_t ook_tx_set_baud(uint32_t baud) {
	rmt_bytes_encoder_config_t enc_cfg = encoder_config(baud);
	esp_err_t err = rmt_bytes_encoder_update_config(ook_encoder, &enc_cfg);
	if (err == ESP_OK) ook_baud = baud;
	return err;
}

esp_err_t ook_tx_send(const uint8_t *data, size_t len) {
	cc1101_t *dev = cc_ook_dev();
	esp_err_t err = cc1101_strobe(dev, CC1101_STX);
	if (err != ESP_OK) return err;
	err = cc1101_wait_marcstate(dev, CC1101_MARCSTATE_TX, OOK_TX_SETTLE_US);
	if (err == ESP_OK) {
		rmt_transmit_config_t tx_cfg = {
				.loop_count      = 0,
				.flags.eot_level = 0, /* carrier off once the data ends */
		};
		err = rmt_transmit(ook_chan, ook_encoder, data, len, &tx_cfg);
	}
	if (err == ESP_OK) {
		uint32_t timeout_ms = (uint32_t)((uint64_t)len * 8 * 1000 / ook_baud) + 100;
		err = rmt_tx_wait_all_done(ook_chan, (int)timeout_ms);
	}
	esp_err_t idle_err = cc1101_strobe(dev, CC1101_SIDLE); /* always leave TX, even after an error */
	return err != ESP_OK ? err : idle_err;
}
