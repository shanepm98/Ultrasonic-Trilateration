#include "espnow_link.h"

#include <stddef.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_now.h"
#include "esp_wifi.h"
#include "nvs_flash.h"

#include "at_common.h"
#include "at_protocol.h"

static const char *TAG = "at-espnow-rx";

static QueueHandle_t g_snapshot_queue;

/* Runs in the WiFi/system task context - must stay non-blocking, no SPI/SonicLib calls. Only
 * length/version/CRC validation happens here; the actual reconfigure runs in at_transmitter.c's
 * apply_task, driven by g_snapshot_queue. */
static void on_espnow_recv(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
    (void)info;

    if (len != (int)sizeof(at_config_snapshot_t)) {
        return;
    }
    const at_config_snapshot_t *snap = (const at_config_snapshot_t *)data;
    if (snap->proto_version != AT_PROTOCOL_VERSION) {
        return;
    }
    uint16_t crc = at_crc16(data, offsetof(at_config_snapshot_t, crc16));
    if (crc != snap->crc16) {
        return;
    }

    xQueueOverwrite(g_snapshot_queue, snap); /* coalesces with any not-yet-applied previous one */
}

esp_err_t espnow_link_init(QueueHandle_t snapshot_queue) {
    g_snapshot_queue = snapshot_queue;

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
    err = esp_now_register_recv_cb(on_espnow_recv);
    if (err != ESP_OK) {
        return err;
    }

    ESP_LOGI(TAG, "ESP-NOW receive link ready (channel %u)", AT_ESPNOW_CHANNEL);
    return ESP_OK;
}
