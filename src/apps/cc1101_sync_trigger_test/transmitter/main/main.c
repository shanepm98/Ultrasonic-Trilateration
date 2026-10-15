/*! \file main.c
 *
 * \brief CC1101 sync-trigger test - transmitter. Configures the CC1101 for 433.92 MHz async ASK,
 * dumps its registers, brings up the ESP-NOW side channel, then runs the console menu
 * (tx_menu.c). See ../../README.md.
 */

#include <stdio.h>

#include "esp_log.h"

#include "cc_link.h"
#include "cc_ask.h"
#include "console_io.h"
#include "ask_tx.h"
#include "tx_menu.h"

static const char *TAG = "cc_tx";

void app_main(void) {
	ESP_ERROR_CHECK(cc_ask_init());
	ESP_ERROR_CHECK(cc_ask_configure(CC_ASK_DEFAULT_BAUD));
	ESP_ERROR_CHECK(ask_tx_init(CC_ASK_DEFAULT_BAUD));

	ESP_LOGI(TAG, "CC1101 configured for async ASK; register dump:");
	ESP_ERROR_CHECK(cc_ask_dump_print());

	ESP_ERROR_CHECK(ask_tx_carrier_on()); /* carrier stays up at the "0" level from here on */
	ESP_LOGI(TAG, "carrier on");

	ESP_ERROR_CHECK(cc_link_init());
	ESP_LOGI(TAG, "ESP-NOW up on channel %d", CC_LINK_CHANNEL);

	console_init();
	tx_menu_run();
}
