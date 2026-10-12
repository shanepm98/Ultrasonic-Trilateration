/*! \file ook_tx.h
 *
 * \brief Async-OOK transmit: the ESP32's RMT peripheral clocks the data bits out on GDO0
 * (CC_OOK_GDO0_GPIO) at the configured bit rate, NRZ, MSB first, while the CC1101 is in TX and
 * keys its carrier from that pin. See ../../README.md.
 */

#ifndef OOK_TX_H_
#define OOK_TX_H_

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/*! \brief Configure the RMT TX channel on GDO0 (idling low = carrier off) at \a baud. Call once. */
esp_err_t ook_tx_init(uint32_t baud);

/*! \brief Change the RMT bit time to 1 / \a baud. Requires ook_tx_init(). */
esp_err_t ook_tx_set_baud(uint32_t baud);

/*!
 * \brief Send \a len bytes: strobe the CC1101 into TX, clock the bits out, wait for the RMT to
 * finish, and strobe back to IDLE. Requires ook_tx_init() and cc_ook_configure().
 */
esp_err_t ook_tx_send(const uint8_t *data, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* OOK_TX_H_ */
