#include "espnow_relay.h"

#include <string.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_now.h"
#include "esp_wifi.h"
#include "nvs_flash.h"

#include "at_common.h"

static const char *TAG = "at-espnow-tx";

static const uint8_t k_broadcast_mac[6] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };

esp_err_t espnow_relay_init(void) {
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    if (err != ESP_OK) {
        return err;
    }

    err = esp_netif_init();
    if (err != ESP_OK) {
        return err;
    }
    err = esp_event_loop_create_default();
    if (err != ESP_OK) {
        return err;
    }

    wifi_init_config_t wifi_cfg = WIFI_INIT_CONFIG_DEFAULT();
    err                         = esp_wifi_init(&wifi_cfg);
    if (err != ESP_OK) {
        return err;
    }
    /* Don't persist WiFi state to flash across resets - a single fixed receiver/transmitter pair
     * on a fixed channel has nothing to gain from NVS-cached config, and stale state there could
     * otherwise silently override the channel set explicitly below. */
    err = esp_wifi_set_storage(WIFI_STORAGE_RAM);
    if (err != ESP_OK) {
        return err;
    }
    err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (err != ESP_OK) {
        return err;
    }
    err = esp_wifi_start();
    if (err != ESP_OK) {
        return err;
    }
    err = esp_wifi_set_channel(AT_ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);
    if (err != ESP_OK) {
        return err;
    }

    err = esp_now_init();
    if (err != ESP_OK) {
        return err;
    }

    esp_now_peer_info_t peer = { 0 };
    memcpy(peer.peer_addr, k_broadcast_mac, sizeof(peer.peer_addr));
    peer.channel = AT_ESPNOW_CHANNEL;
    peer.ifidx   = WIFI_IF_STA;
    peer.encrypt = false;
    err          = esp_now_add_peer(&peer);
    if (err != ESP_OK) {
        return err;
    }

    ESP_LOGI(TAG, "ESP-NOW relay ready (broadcast, channel %u)", AT_ESPNOW_CHANNEL);
    return ESP_OK;
}

esp_err_t espnow_relay_send(const void *data, size_t len) {
    return esp_now_send(k_broadcast_mac, (const uint8_t *)data, len);
}
