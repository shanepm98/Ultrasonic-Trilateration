/*! \file cc_link.c
 *
 * \brief Implementation of the ESP-NOW side channel. See cc_link.h.
 */

#include "cc_link.h"

#include <string.h>

#include "esp_event.h"
#include "esp_now.h"
#include "esp_wifi.h"
#include "freertos/queue.h"
#include "nvs_flash.h"

#define CC_LINK_QUEUE_LEN 8

static const uint8_t cc_link_bcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
static QueueHandle_t cc_link_queue;

/* Runs in the Wi-Fi task: copy and queue only, never block. A full queue drops the frame. */
static void on_recv(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
	cc_link_msg_t msg;
	if (len <= 0) return;
	if (len > CC_LINK_MAX_LEN) len = CC_LINK_MAX_LEN;
	memcpy(msg.src, info->src_addr, sizeof(msg.src));
	memcpy(msg.text, data, len);
	msg.text[len] = '\0';
	xQueueSend(cc_link_queue, &msg, 0);
}

esp_err_t cc_link_init(void) {
	cc_link_queue = xQueueCreate(CC_LINK_QUEUE_LEN, sizeof(cc_link_msg_t));
	if (cc_link_queue == NULL) return ESP_ERR_NO_MEM;

	esp_err_t err = nvs_flash_init(); /* Wi-Fi keeps its PHY calibration in NVS */
	if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
		err = nvs_flash_erase();
		if (err == ESP_OK) err = nvs_flash_init();
	}
	if (err != ESP_OK) return err;

	err = esp_event_loop_create_default();
	if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return err; /* already created is fine */

	wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
	err = esp_wifi_init(&cfg);
	if (err != ESP_OK) return err;
	err = esp_wifi_set_storage(WIFI_STORAGE_RAM);
	if (err != ESP_OK) return err;
	err = esp_wifi_set_mode(WIFI_MODE_STA);
	if (err != ESP_OK) return err;
	err = esp_wifi_start();
	if (err != ESP_OK) return err;
	err = esp_wifi_set_channel(CC_LINK_CHANNEL, WIFI_SECOND_CHAN_NONE);
	if (err != ESP_OK) return err;

	err = esp_now_init();
	if (err != ESP_OK) return err;
	err = esp_now_register_recv_cb(on_recv);
	if (err != ESP_OK) return err;

	esp_now_peer_info_t peer = {
			.channel = CC_LINK_CHANNEL,
			.ifidx   = WIFI_IF_STA,
			.encrypt = false,
	};
	memcpy(peer.peer_addr, cc_link_bcast, sizeof(peer.peer_addr));
	return esp_now_add_peer(&peer);
}

esp_err_t cc_link_broadcast(const char *text) {
	size_t len = strlen(text);
	if (len == 0 || len > CC_LINK_MAX_LEN) return ESP_ERR_INVALID_ARG;
	return esp_now_send(cc_link_bcast, (const uint8_t *)text, len);
}

bool cc_link_receive(cc_link_msg_t *msg, TickType_t timeout) {
	return xQueueReceive(cc_link_queue, msg, timeout) == pdTRUE;
}
