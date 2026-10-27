/*! \file main.c
 *
 * \brief CC1101 sync-trigger test - transmitter. Loads the trigger config picked in cc_cfg_select.h
 * (433.92 MHz async ASK or FSK), dumps its registers, brings up the ESP-NOW side channel, then runs
 * the console menu (tx_menu.c). See ../../README.md.
 */

#include <stdio.h>

#include "esp_log.h"

#include "cc_link.h"
#include "cc_radio.h"
#include "cc_cfg_select.h"
#include "console_io.h"
#include "radio_tx.h"
#include "tx_menu.h"

static const char *TAG = "cc_tx";

void app_main(void) {
	ESP_ERROR_CHECK(cc_radio_init());
	ESP_ERROR_CHECK(cc_radio_load(CC_CFG_ACTIVE->tx_regs, CC_CFG_ACTIVE->patable));
	ESP_ERROR_CHECK(radio_tx_init(CC_CFG_ACTIVE->tx_baud));

	ESP_LOGI(TAG, "CC1101 configured for async %s; register dump:", CC_CFG_ACTIVE->name);
	ESP_ERROR_CHECK(cc_radio_dump_print());

	ESP_ERROR_CHECK(radio_tx_carrier_on()); /* carrier stays up at the "0" symbol from here on */
	ESP_LOGI(TAG, "carrier on");

	ESP_ERROR_CHECK(cc_link_init());
	ESP_LOGI(TAG, "ESP-NOW up on channel %d", CC_LINK_CHANNEL);

	console_init();
	tx_menu_run(CC_CFG_ACTIVE->tx_baud);
}
