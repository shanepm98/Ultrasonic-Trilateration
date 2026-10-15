/*! \file tx_menu.h
 *
 * \brief The transmitter's interactive console menu. See ../../README.md.
 */

#ifndef TX_MENU_H_
#define TX_MENU_H_

#ifdef __cplusplus
extern "C" {
#endif

/*!
 * \brief Run the menu forever. Requires console_init(), cc_ask_configure() at CC_ASK_DEFAULT_BAUD,
 * ask_tx_init(), ask_tx_carrier_on(), and cc_link_init().
 */
void tx_menu_run(void);

#ifdef __cplusplus
}
#endif

#endif /* TX_MENU_H_ */
