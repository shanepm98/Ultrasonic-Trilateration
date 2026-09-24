/* Must precede every include (even transitive ones) - see Kconfig.projbuild's FTSP_MUTE_LOGS. */
#ifdef FTSP_MUTE_LOGS
#define LOG_LOCAL_LEVEL ESP_LOG_NONE
#endif

#include "espnow_link.h"

#include <inttypes.h>
#include <string.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_now.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "nvs_flash.h"

static const char *TAG = "ftsp-espnow";

static const uint8_t BROADCAST_MAC[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

static QueueHandle_t g_rx_queue;
static uint32_t      g_rx_drop_count;

/* Runs in the WiFi/system task context - must stay non-blocking, no FreeRTOS-blocking or heavy
 * work here. Timestamp capture happens before anything else to keep it as close as possible to
 * the actual radio RX event; everything after that is just validation + a queue push. */
static void on_espnow_recv(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
    int64_t now_us = esp_timer_get_time();

    if (len <= 0 || (size_t)len > FTSP_ESPNOW_MAX_PAYLOAD) {
        return;
    }

    espnow_link_rx_event_t evt;
    evt.recv_local_us = now_us;
    evt.len            = (uint8_t)len;
    memcpy(evt.src_mac, info->src_addr, sizeof(evt.src_mac));
    memcpy(evt.data, data, (size_t)len);

    if (xQueueSend(g_rx_queue, &evt, 0) != pdTRUE) {
        g_rx_drop_count++;
        if (g_rx_drop_count % 50 == 1) {
            ESP_LOGW(TAG, "rx queue full - dropped %" PRIu32 " packet(s) so far", g_rx_drop_count);
        }
    }
}

esp_err_t espnow_link_init(QueueHandle_t rx_queue) {
    g_rx_queue = rx_queue;

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
    err = esp_wifi_set_channel(FTSP_ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);
    if (err != ESP_OK) {
        return err;
    }
    /* STA mode defaults to WIFI_PS_MIN_MODEM, which sleeps the radio between operations and
     * wakes it on demand - a well-known source of multi-millisecond, variable latency on
     * esp_now_send()/recv-callback dispatch. Timing-critical, so disable it outright: every
     * FTSP timestamp is taken at the API call site, and this class of latency corrupts them
     * directly (unlike WiFi driver queuing jitter, which is small and roughly constant). */
    err = esp_wifi_set_ps(WIFI_PS_NONE);
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

    esp_now_peer_info_t peer = {0};
    memcpy(peer.peer_addr, BROADCAST_MAC, sizeof(peer.peer_addr));
    peer.channel = FTSP_ESPNOW_CHANNEL;
    peer.encrypt = false;
    peer.ifidx   = WIFI_IF_STA;
    err          = esp_now_add_peer(&peer);
    if (err != ESP_OK) {
        return err;
    }

    ESP_LOGI(TAG, "ESP-NOW link ready (channel %u)", FTSP_ESPNOW_CHANNEL);
    return ESP_OK;
}

esp_err_t espnow_link_send(const void *data, size_t len) {
    return esp_now_send(BROADCAST_MAC, (const uint8_t *)data, len);
}

esp_err_t espnow_link_get_own_mac(uint8_t mac_out[6]) {
    return esp_wifi_get_mac(WIFI_IF_STA, mac_out);
}
