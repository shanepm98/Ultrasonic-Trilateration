/*! \file tx_menu.h
 *
 * \brief The transmitter's interactive console menu. See ../../README.md.
 */

#ifndef TX_MENU_H_
#define TX_MENU_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*!
 * \brief Run the menu forever, starting at the bit rate \a baud the RMT and CC1101 were set up for.
 * Requires console_init(), cc_radio_load() (or cc_radio_configure()), radio_tx_init(\a baud),
 * radio_tx_carrier_on(), and cc_link_init().
 */
void tx_menu_run(uint32_t baud);

#ifdef __cplusplus
}
#endif

#endif /* TX_MENU_H_ */
