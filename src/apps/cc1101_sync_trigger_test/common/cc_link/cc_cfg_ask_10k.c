/*! \file cc_cfg_ask_10k.c
 *
 * \brief ASK trigger config: TX 10 kBaud, RX 160 kBaud, 541 kHz filter, AGC 05/00/91. See cc_cfg.h.
 *
 * Recommended. 500 shots (2026-10-26): median skew -0.17 us, p90 |skew| 0.78 us, p95 0.90 us,
 * p99 1.14 us, max 1.36 us, 14/500 shots over 1 us, no whole-bit outliers.
 * skew_sweep.py config ASK_tx10_rx160_bw541_agc050091.
 */

#include "cc_cfg.h"

const cc_cfg_t cc_cfg_ask_10k = {
		.name    = "ASK TX 10k / RX 160k / 541 kHz / AGC 05",
		.tx_regs = {
			[CC1101_IOCFG2]   = 0x0D, /* GDO2: serial data output (async) -> oscilloscope / INT1 */
			[CC1101_IOCFG1]   = 0x2E, /* GDO1 (= SO/MISO): high-Z when CSn is high - shared bus */
			[CC1101_IOCFG0]   = 0x2E, /* GDO0: output high-Z; it is the async TX data input */
			[CC1101_FIFOTHR]  = 0x47,
			[CC1101_SYNC1]    = 0xD3,
			[CC1101_SYNC0]    = 0x91,
			[CC1101_PKTLEN]   = 0xFF,
			[CC1101_PKTCTRL1] = 0x00,
			[CC1101_PKTCTRL0] = 0x32, /* PKT_FORMAT = 3 (async serial), no CRC, infinite length */
			[CC1101_ADDR]     = 0x00,
			[CC1101_CHANNR]   = 0x00,
			[CC1101_FSCTRL1]  = 0x06, /* IF = 152 kHz (SmartRF Studio) */
			[CC1101_FSCTRL0]  = 0x00,
			[CC1101_FREQ2]    = 0x10, /* 433.92 MHz = 0x10B071 * 26 MHz / 2^16 */
			[CC1101_FREQ1]    = 0xB0,
			[CC1101_FREQ0]    = 0x71,
			[CC1101_MDMCFG4]  = 0x28, /* CHANBW 541 kHz (unused in TX); DRATE_E = 8 */
			[CC1101_MDMCFG3]  = 0x93, /* DRATE_M: 9.993 kBaud */
			[CC1101_MDMCFG2]  = 0x30, /* ASK/OOK, Manchester off, SYNC_MODE = 0 (no preamble/sync) */
			[CC1101_MDMCFG1]  = 0x02,
			[CC1101_MDMCFG0]  = 0xF8,
			[CC1101_DEVIATN]  = 0x47, /* reset default (FSK only) */
			[CC1101_MCSM2]    = 0x07,
			[CC1101_MCSM1]    = 0x30,
			[CC1101_MCSM0]    = 0x18, /* FS_AUTOCAL = 1: calibrate on IDLE -> RX/TX; PO_TIMEOUT 149 us */
			[CC1101_FOCCFG]   = 0x16,
			[CC1101_BSCFG]    = 0x6C,
			[CC1101_AGCCTRL2] = 0x03, /* AGC is RX-only; cc_ask.c default */
			[CC1101_AGCCTRL1] = 0x00,
			[CC1101_AGCCTRL0] = 0x91,
			[CC1101_WOREVT1]  = 0x87,
			[CC1101_WOREVT0]  = 0x6B,
			[CC1101_WORCTRL]  = 0xFB,
			[CC1101_FREND1]   = 0x56,
			[CC1101_FREND0]   = 0x11, /* PA_POWER = 1: ASK switches between PATABLE[0] and [1]; FSK transmits at PATABLE[1] */
			[CC1101_FSCAL3]   = 0xE9, /* FSCAL3..1: SmartRF Studio values; the chip recalibrates them (FS_AUTOCAL) */
			[CC1101_FSCAL2]   = 0x2A,
			[CC1101_FSCAL1]   = 0x00,
			[CC1101_FSCAL0]   = 0x1F,
			[CC1101_RCCTRL1]  = 0x41,
			[CC1101_RCCTRL0]  = 0x00,
			[CC1101_FSTEST]   = 0x59,
			[CC1101_PTEST]    = 0x7F,
			[CC1101_AGCTEST]  = 0x3F, /* SmartRF Studio value (reads back varying) */
			[CC1101_TEST2]    = 0x81,
			[CC1101_TEST1]    = 0x35,
			[CC1101_TEST0]    = 0x09,
		},
		.rx_regs = {
			[CC1101_IOCFG2]   = 0x0D, /* GDO2: serial data output (async) -> oscilloscope / INT1 */
			[CC1101_IOCFG1]   = 0x2E, /* GDO1 (= SO/MISO): high-Z when CSn is high - shared bus */
			[CC1101_IOCFG0]   = 0x2E, /* GDO0: output high-Z; it is the async TX data input */
			[CC1101_FIFOTHR]  = 0x47,
			[CC1101_SYNC1]    = 0xD3,
			[CC1101_SYNC0]    = 0x91,
			[CC1101_PKTLEN]   = 0xFF,
			[CC1101_PKTCTRL1] = 0x00,
			[CC1101_PKTCTRL0] = 0x32, /* PKT_FORMAT = 3 (async serial), no CRC, infinite length */
			[CC1101_ADDR]     = 0x00,
			[CC1101_CHANNR]   = 0x00,
			[CC1101_FSCTRL1]  = 0x06, /* IF = 152 kHz (SmartRF Studio) */
			[CC1101_FSCTRL0]  = 0x00,
			[CC1101_FREQ2]    = 0x10, /* 433.92 MHz = 0x10B071 * 26 MHz / 2^16 */
			[CC1101_FREQ1]    = 0xB0,
			[CC1101_FREQ0]    = 0x71,
			[CC1101_MDMCFG4]  = 0x2C, /* CHANBW_E = 0, CHANBW_M = 2: RX filter 541 kHz; DRATE_E = 12 */
			[CC1101_MDMCFG3]  = 0x93, /* DRATE_M: 159.88 kBaud (sets the async sampling) */
			[CC1101_MDMCFG2]  = 0x30, /* ASK/OOK, Manchester off, SYNC_MODE = 0 (no preamble/sync) */
			[CC1101_MDMCFG1]  = 0x02,
			[CC1101_MDMCFG0]  = 0xF8,
			[CC1101_DEVIATN]  = 0x47, /* reset default (FSK only) */
			[CC1101_MCSM2]    = 0x07,
			[CC1101_MCSM1]    = 0x30,
			[CC1101_MCSM0]    = 0x18, /* FS_AUTOCAL = 1: calibrate on IDLE -> RX/TX; PO_TIMEOUT 149 us */
			[CC1101_FOCCFG]   = 0x16,
			[CC1101_BSCFG]    = 0x6C,
			[CC1101_AGCCTRL2] = 0x05, /* MAX_DVGA_GAIN all, MAX_LNA_GAIN max, MAGN_TARGET 5 = 40 dB - best for ASK in the sweep */
			[CC1101_AGCCTRL1] = 0x00, /* DN022 */
			[CC1101_AGCCTRL0] = 0x91, /* DN022 */
			[CC1101_WOREVT1]  = 0x87,
			[CC1101_WOREVT0]  = 0x6B,
			[CC1101_WORCTRL]  = 0xFB,
			[CC1101_FREND1]   = 0x56,
			[CC1101_FREND0]   = 0x11, /* PA_POWER = 1: ASK switches between PATABLE[0] and [1]; FSK transmits at PATABLE[1] */
			[CC1101_FSCAL3]   = 0xE9, /* FSCAL3..1: SmartRF Studio values; the chip recalibrates them (FS_AUTOCAL) */
			[CC1101_FSCAL2]   = 0x2A,
			[CC1101_FSCAL1]   = 0x00,
			[CC1101_FSCAL0]   = 0x1F,
			[CC1101_RCCTRL1]  = 0x41,
			[CC1101_RCCTRL0]  = 0x00,
			[CC1101_FSTEST]   = 0x59,
			[CC1101_PTEST]    = 0x7F,
			[CC1101_AGCTEST]  = 0x3F, /* SmartRF Studio value (reads back varying) */
			[CC1101_TEST2]    = 0x81,
			[CC1101_TEST1]    = 0x35,
			[CC1101_TEST0]    = 0x09,
		},
		.patable = {CC_ASK_PA_LOW, CC_ASK_PA_HIGH},
		.tx_baud = 10000,
};
