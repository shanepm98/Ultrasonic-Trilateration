/*! \file espnow_link.h
 *
 * \brief Thin ESP-NOW transport: WiFi STA bring-up (no AP association), broadcast send, and a
 * receive callback that timestamps each inbound packet and hands it to ftsp_sync.c via a queue.
 * This layer knows nothing about the FTSP wire protocol - see ftsp_sync.h for that.
 */

#ifndef ESPNOW_LINK_H_
#define ESPNOW_LINK_H_

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#ifdef __cplusplus
extern "C" {
#endif

#define FTSP_ESPNOW_CHANNEL     1u
#define FTSP_ESPNOW_MAX_PAYLOAD 64u /* headroom vs. the actual wire structs; well under ESP-NOW's 250B cap */

/*! \brief One inbound ESP-NOW packet, timestamped and queued for ftsp_sync's task. */
typedef struct {
    uint8_t src_mac[6];
    int64_t recv_local_us; /* esp_timer_get_time(), captured first thing in the recv callback */
    uint8_t len;
    uint8_t data[FTSP_ESPNOW_MAX_PAYLOAD];
} espnow_link_rx_event_t;

/*! \brief Bring up WiFi STA + ESP-NOW, add the broadcast peer, and register the receive
 * callback. Call once, before ftsp_sync_start(). Every packet that passes a basic length check
 * is pushed (by value) onto rx_queue; the queue is sized/owned by the caller. */
esp_err_t espnow_link_init(QueueHandle_t rx_queue);

/*! \brief Broadcast len bytes of data to the ESP-NOW broadcast peer. */
esp_err_t espnow_link_send(const void *data, size_t len);

/*! \brief Read this device's own STA MAC address (used by ftsp_sync for root election). */
esp_err_t espnow_link_get_own_mac(uint8_t mac_out[6]);

#ifdef __cplusplus
}
#endif

#endif /* ESPNOW_LINK_H_ */
