/*! \file espnow_relay.h
 *
 * \brief Receiver-side ESP-NOW send-only link to the transmitter: WiFi STA bring-up (no AP
 * association), ESP-NOW init, and a broadcast peer. No SonicLib/protocol-struct knowledge beyond
 * moving raw bytes - at_sensor_config.c decides what and when to send.
 */

#ifndef ESPNOW_RELAY_H_
#define ESPNOW_RELAY_H_

#include <stddef.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/*! \brief Bring up WiFi STA + ESP-NOW and add the broadcast peer. Call once, before any
 * espnow_relay_send(). */
esp_err_t espnow_relay_init(void);

/*! \brief Fire-and-forget send to the broadcast peer. The wire protocol (at_config_snapshot_t)
 * is already designed to tolerate a dropped packet, so the return value is informational only. */
esp_err_t espnow_relay_send(const void *data, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* ESPNOW_RELAY_H_ */
