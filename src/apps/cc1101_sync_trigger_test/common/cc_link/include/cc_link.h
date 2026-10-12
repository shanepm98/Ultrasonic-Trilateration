/*! \file cc_link.h
 *
 * \brief ESP-NOW broadcast side channel between the transmitter and the receiver nodes, used to
 * reconfigure and read back each node's CC1101 without reflashing or extra wiring. Wi-Fi runs in
 * station mode, unconnected, on channel CC_LINK_CHANNEL. Messages are the ASCII strings in
 * cc_proto.h.
 *
 * The ESP-NOW receive callback runs in the Wi-Fi task, so it only copies each frame into a queue;
 * cc_link_receive() hands them to the application task.
 */

#ifndef CC_LINK_H_
#define CC_LINK_H_

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CC_LINK_CHANNEL 1
#define CC_LINK_MAX_LEN 250 /* ESP-NOW v1 payload limit */

/*! \brief One received frame. \a text is NUL-terminated. */
typedef struct {
	uint8_t src[6];
	char    text[CC_LINK_MAX_LEN + 1];
} cc_link_msg_t;

/*! \brief Bring up NVS, Wi-Fi (STA, channel CC_LINK_CHANNEL) and ESP-NOW with a broadcast peer. Call once. */
esp_err_t cc_link_init(void);

/*! \brief Broadcast \a text (without its NUL; at most CC_LINK_MAX_LEN chars). Requires cc_link_init(). */
esp_err_t cc_link_broadcast(const char *text);

/*! \brief Wait up to \a timeout for the next received frame. Requires cc_link_init(). */
bool cc_link_receive(cc_link_msg_t *msg, TickType_t timeout);

#ifdef __cplusplus
}
#endif

#endif /* CC_LINK_H_ */
