/*! \file radio_tx.h
 *
 * \brief Async transmit: the ESP32's RMT peripheral clocks the data bits out on GDO0
 * (CC_RADIO_GDO0_GPIO) at the configured bit rate, NRZ, MSB first, while the CC1101 is in TX and
 * keys its carrier from that pin: ASK switches between PATABLE[0] (0) and PATABLE[1] (1), FSK
 * between the low and high deviation.
 *
 * The carrier stays on continuously at the "0" symbol between sends (radio_tx_carrier_on()), so the
 * receivers always see a carrier. Reconfigure the radio only between radio_tx_carrier_off() and
 * radio_tx_carrier_on(). See ../../README.md.
 */

#ifndef RADIO_TX_H_
#define RADIO_TX_H_

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/*! \brief Configure the RMT TX channel on GDO0 (idling low = carrier off) at \a baud. Call once. */
esp_err_t radio_tx_init(uint32_t baud);

/*! \brief Change the RMT bit time to 1 / \a baud. Requires radio_tx_init(). */
esp_err_t radio_tx_set_baud(uint32_t baud);

/*!
 * \brief Strobe the CC1101 into TX and wait until it is there. GDO0 idles low, so the carrier
 * comes up at the "0" symbol. Requires radio_tx_init() and cc_radio_load().
 */
esp_err_t radio_tx_carrier_on(void);

/*! \brief Strobe the CC1101 to IDLE (carrier off), e.g. to rewrite registers. Requires cc_radio_init(). */
esp_err_t radio_tx_carrier_off(void);

/*!
 * \brief Send \a len bytes on the running carrier and wait for the RMT to finish. GDO0 returns low
 * (the "0" symbol) afterwards. Requires radio_tx_carrier_on().
 */
esp_err_t radio_tx_send(const uint8_t *data, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* RADIO_TX_H_ */
