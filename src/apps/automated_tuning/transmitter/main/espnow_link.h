/*! \file espnow_link.h
 *
 * \brief Transmitter-side ESP-NOW receive-only link from the receiver: WiFi STA bring-up (no AP
 * association) and ESP-NOW init. Validates each inbound packet (length, protocol version,
 * CRC-16) before handing a copy to at_transmitter.c via a queue - the ESP-NOW receive callback
 * itself must stay non-blocking (WiFi/system task context), so it does no SonicLib/SPI work.
 */

#ifndef ESPNOW_LINK_H_
#define ESPNOW_LINK_H_

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#ifdef __cplusplus
extern "C" {
#endif

/*! \brief Bring up WiFi STA + ESP-NOW and register the receive callback. Call once. Valid,
 * crc-checked, correct-proto_version at_config_snapshot_t packets are pushed (by value, via
 * xQueueOverwrite) onto snapshot_queue; anything else is silently dropped inside the callback. */
esp_err_t espnow_link_init(QueueHandle_t snapshot_queue);

#ifdef __cplusplus
}
#endif

#endif /* ESPNOW_LINK_H_ */
