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
 * \brief Run the menu forever. Requires console_init(), cc_ook_configure() at CC_OOK_DEFAULT_BAUD,
 * ook_tx_init(), and cc_link_init().
 */
void tx_menu_run(void);

#ifdef __cplusplus
}
#endif

#endif /* TX_MENU_H_ */
