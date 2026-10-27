/*! \file cc_cfg_select.h
 *
 * \brief Picks which trigger config (cc_cfg.h) the transmitter and every receiver load at boot.
 * Both apps include this file, so they always agree. Change CC_CFG_SELECT, then rebuild and reflash
 * the transmitter and every receiver.
 */

#ifndef CC_CFG_SELECT_H_
#define CC_CFG_SELECT_H_

#include "cc_cfg.h"

#define CC_CFG_ASK 0 /* cc_cfg_ask_10k.c: ASK, TX 10k, RX 160k, 541 kHz, AGC 05/00/91 (recommended) */
#define CC_CFG_FSK 1 /* cc_cfg_fsk_50k.c: 2-FSK, TX 50k, RX 160k, 541 kHz, AGC C3/00/91 */

#ifndef CC_CFG_SELECT
#define CC_CFG_SELECT CC_CFG_ASK
#endif

#if CC_CFG_SELECT == CC_CFG_ASK
#define CC_CFG_ACTIVE (&cc_cfg_ask_10k)
#elif CC_CFG_SELECT == CC_CFG_FSK
#define CC_CFG_ACTIVE (&cc_cfg_fsk_50k)
#else
#error "CC_CFG_SELECT must be CC_CFG_ASK or CC_CFG_FSK"
#endif

#endif /* CC_CFG_SELECT_H_ */
