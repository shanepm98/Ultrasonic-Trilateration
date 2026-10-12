/*! \file main.c
 *
 * \brief CC1101 sync-trigger test - transmitter. Configures the CC1101 for 433.92 MHz async OOK,
 * dumps its registers, brings up the ESP-NOW side channel, then runs the console menu
 * (tx_menu.c). See ../../README.md.
 */

#include <stdio.h>

#include "esp_log.h"

#include "cc_link.h"
#include "cc_ook.h"
#include "console_io.h"
#include "ook_tx.h"
#include "tx_menu.h"

static const char *TAG = "cc_tx";

void app_main(void) {
	ESP_ERROR_CHECK(cc_ook_init());
	ESP_ERROR_CHECK(cc_ook_configure(CC_OOK_DEFAULT_BAUD));
	ESP_ERROR_CHECK(ook_tx_init(CC_OOK_DEFAULT_BAUD));

	ESP_LOGI(TAG, "CC1101 configured for async OOK; register dump:");
	ESP_ERROR_CHECK(cc_ook_dump_print());

	ESP_ERROR_CHECK(cc_link_init());
	ESP_LOGI(TAG, "ESP-NOW up on channel %d", CC_LINK_CHANNEL);

	console_init();
	tx_menu_run();
}
