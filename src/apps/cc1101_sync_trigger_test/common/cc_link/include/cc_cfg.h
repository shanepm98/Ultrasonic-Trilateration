/*! \file cc_cfg.h
 *
 * \brief Complete CC1101 register sets for the two best trigger configs from the skew sweeps
 * (docs/cc1101_skew_sweep_summary.md, results_500shot_20261026.md). One file per config:
 *
 * - cc_cfg_ask_10k.c: ASK, TX 10 kBaud, RX 160 kBaud, 541 kHz filter, AGC 05/00/91.
 *   500 shots: p95 |skew| 0.90 us, max 1.36 us, no whole-bit outliers. Recommended.
 * - cc_cfg_fsk_50k.c: 2-FSK, TX 50 kBaud (25.4 kHz deviation), RX 160 kBaud, 541 kHz filter,
 *   AGC C3/00/91. 500 shots: p95 0.67 us, but 4 whole-bit outliers (up to 119 us).
 *
 * The transmitter and the receivers differ only in the data rate (MDMCFG4 low nibble, MDMCFG3):
 * a receiver in async mode has no bit synchronizer, and a higher RX data rate samples the
 * demodulator output more finely, so the receivers run at 160 kBaud whatever the TX bit rate is.
 * The values are the live register dumps the sweeps ran with; FSCAL3..1 and AGCTEST hold the
 * SmartRF Studio values from cc_ask.c, since the chip overwrites them itself.
 */

#ifndef CC_CFG_H_
#define CC_CFG_H_

#include <stdint.h>

#include "cc_ask.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
	const char *name;
	uint8_t     tx_regs[CC_ASK_DUMP_CONFIG_LEN]; /* 0x00-0x2E for the transmitter, one burst write */
	uint8_t     rx_regs[CC_ASK_DUMP_CONFIG_LEN]; /* 0x00-0x2E for every receiver */
	uint8_t     patable[2];                      /* PATABLE[0..1] */
	uint32_t    tx_baud;                         /* the transmitter's RMT bit rate; matches tx_regs */
} cc_cfg_t;

extern const cc_cfg_t cc_cfg_ask_10k;
extern const cc_cfg_t cc_cfg_fsk_50k;

#ifdef __cplusplus
}
#endif

#endif /* CC_CFG_H_ */
