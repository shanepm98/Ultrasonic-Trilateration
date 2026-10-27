/*! \file main.c
 *
 * \brief CC1101 sync-trigger test - receiver. Loads the trigger config picked in cc_cfg_select.h
 * (433.92 MHz async ASK or FSK), dumps its registers, and parks the radio in RX, with the
 * demodulated data on GDO2 for the oscilloscope. Then serves the ESP-NOW side channel: "RR:VV" writes a register, "DUMP" replies
 * with this node's registers (cc_proto.h). See ../../README.md.
 */

#include <stdio.h>

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "cc1101.h"
#include "cc_link.h"
#include "cc_radio.h"
#include "cc_cfg_select.h"
#include "cc_proto.h"

#define RX_SETTLE_US         2000 /* IDLE -> RX incl. autocal is ~800 us */
#define RX_REPLY_JITTER_MS   100  /* random delay before a DUMP reply so receivers don't collide */

static const char *TAG = "cc_rx";

/* Requires cc_radio_init(). */
static esp_err_t enter_rx(void) {
	esp_err_t err = cc1101_strobe(cc_radio_dev(), CC1101_SRX);
	if (err != ESP_OK) return err;
	return cc1101_wait_marcstate(cc_radio_dev(), CC1101_MARCSTATE_RX, RX_SETTLE_US);
}

static void handle_write(uint8_t reg, uint8_t value) {
	uint8_t   old = 0, now = 0;
	esp_err_t err = cc1101_strobe(cc_radio_dev(), CC1101_SIDLE); /* reconfigure in IDLE, recalibrate on SRX */
	if (err == ESP_OK) err = cc1101_wait_marcstate(cc_radio_dev(), CC1101_MARCSTATE_IDLE, RX_SETTLE_US);
	if (err == ESP_OK) err = cc_radio_read(reg, &old);
	if (err == ESP_OK) err = cc_radio_write(reg, value);
	if (err == ESP_OK) err = cc_radio_read(reg, &now);
	esp_err_t rx_err = enter_rx();
	if (err == ESP_OK) err = rx_err;
	if (err != ESP_OK) {
		ESP_LOGE(TAG, "write 0x%02X = 0x%02X failed: %s", reg, value, esp_err_to_name(err));
		return;
	}
	ESP_LOGI(TAG, "reg 0x%02X: 0x%02X -> 0x%02X", reg, old, now);
}

static void handle_dump(void) {
	uint8_t   dump[CC_RADIO_DUMP_LEN];
	esp_err_t err = cc_radio_dump_pack(dump);
	if (err != ESP_OK) {
		ESP_LOGE(TAG, "register dump failed: %s", esp_err_to_name(err));
		return;
	}
	char reply[CC_PROTO_REGS_LEN + 1];
	cc_proto_format_regs(reply, dump);
	vTaskDelay(pdMS_TO_TICKS(esp_random() % RX_REPLY_JITTER_MS) + 1);
	err = cc_link_broadcast(reply);
	ESP_LOGI(TAG, "dump requested; reply %s", esp_err_to_name(err));
}

void app_main(void) {
	/* GDO0 is wired to GPIO27 but unused on the receiver; keep the ESP32 side high-impedance. */
	gpio_config_t gdo0_cfg = {
			.pin_bit_mask = (1ULL << CC_RADIO_GDO0_GPIO),
			.mode         = GPIO_MODE_INPUT,
			.pull_up_en   = GPIO_PULLUP_DISABLE,
			.pull_down_en = GPIO_PULLDOWN_DISABLE,
			.intr_type    = GPIO_INTR_DISABLE,
	};
	ESP_ERROR_CHECK(gpio_config(&gdo0_cfg));

	ESP_ERROR_CHECK(cc_radio_init());
	ESP_ERROR_CHECK(cc_radio_load(CC_CFG_ACTIVE->rx_regs, CC_CFG_ACTIVE->patable));
	ESP_ERROR_CHECK(enter_rx());

	ESP_LOGI(TAG, "CC1101 configured for async %s and in RX; register dump:", CC_CFG_ACTIVE->name);
	ESP_ERROR_CHECK(cc_radio_dump_print());

	ESP_ERROR_CHECK(cc_link_init());
	ESP_LOGI(TAG, "ESP-NOW up on channel %d; listening for commands", CC_LINK_CHANNEL);

	static cc_link_msg_t msg; /* large - keep it off the stack */
	for (;;) {
		if (!cc_link_receive(&msg, portMAX_DELAY)) continue;

		uint8_t reg, value;
		if (cc_proto_parse_write(msg.text, &reg, &value)) {
			handle_write(reg, value);
		} else if (cc_proto_is_dump(msg.text)) {
			handle_dump();
		}
		/* anything else (e.g. other receivers' REGS: replies) is ignored */
	}
}
