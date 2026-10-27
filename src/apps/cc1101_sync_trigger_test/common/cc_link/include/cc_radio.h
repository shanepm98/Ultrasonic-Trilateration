/*! \file cc_radio.h
 *
 * \brief The node's CC1101 at 433.92 MHz in asynchronous serial mode. The register set (ASK or
 * 2-FSK) comes from a cc_cfg_t, picked in cc_cfg_select.h and written with cc_radio_load().
 *
 * Asynchronous serial mode (PKTCTRL0.PKT_FORMAT = 3) bypasses the packet handler and FIFOs: in TX
 * the modulator keys the carrier directly from the level on GDO0 (ESP32 GPIO27), and in RX the
 * demodulated data comes straight out on GDO2 (IOCFG2 = 0x0D) for the oscilloscope.
 *
 * Either way the carrier is always present: the transmitter stays in TX between sends, so the
 * receivers' AGC holds on a steady carrier instead of turning its gain up to noise.
 * - ASK (MDMCFG2.MOD_FORMAT = 3) switches the PA between PATABLE[0] (data 0) and PATABLE[1]
 *   (data 1). PATABLE[0] is a lower but non-zero power, so this is ASK rather than OOK.
 * - 2-FSK (MOD_FORMAT = 0) shifts the frequency by +-DEVIATN at a constant PATABLE[1] power.
 *
 * The CC1101 shares SPI2 (MOSI 23 / MISO 19 / SCLK 18) with the ICU-20201, whose chip select
 * (GPIO5) is held high here so the sensor stays off the bus.
 */

#ifndef CC_RADIO_H_
#define CC_RADIO_H_

#include <stdbool.h>
#include <stdint.h>

#include "cc1101.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CC_RADIO_CS_GPIO   GPIO_NUM_25 /* CC1101 CSn */
#define CC_RADIO_GDO0_GPIO GPIO_NUM_27 /* CC1101 GDO0 - async TX data input */

#define CC_RADIO_MIN_BAUD 600u
#define CC_RADIO_MAX_BAUD 250000u /* ASK/OOK maximum (2-FSK goes to 500k) - datasheet Table 3 */

/* PA levels at 433 MHz (DN013): 0xC0 ~ +10 dBm, 0x60 ~ 0 dBm, 0x34 ~ -10 dBm, 0x1D ~ -15 dBm,
 * 0x0E ~ -20 dBm, 0x12 ~ -30 dBm, 0x00 = off. LOW/HIGH = 20 dB modulation depth by default. */
#define CC_RADIO_PA_LOW  0x34 /* PATABLE[0]: the ASK "0" level */
#define CC_RADIO_PA_HIGH 0xC0 /* PATABLE[1]: the ASK "1" level, and the FSK power (FREND0.PA_POWER = 1) */

/* Register addresses cc_radio_write()/cc_radio_read() accept besides config 0x00-0x2E. 0x3E is the
 * chip's own PATABLE address (a single write lands in PATABLE[0]); 0x3F is a pseudo-address used
 * by this app for PATABLE[1] (on the chip, 0x3F is the FIFO). */
#define CC_RADIO_ADDR_PA_LOW  CC1101_PATABLE        /* 0x3E: PATABLE[0] */
#define CC_RADIO_ADDR_PA_HIGH (CC1101_PATABLE + 1)  /* 0x3F: PATABLE[1] */

/*! \brief Register dump: config 0x00-0x2E, then status 0x30-0x3D (read as 0xF0-0xFD). */
#define CC_RADIO_DUMP_CONFIG_LEN (CC1101_LAST_CONFIG_REG + 1)
#define CC_RADIO_DUMP_STATUS_LEN (CC1101_LAST_STATUS_REG - CC1101_FIRST_STATUS_REG + 1)
#define CC_RADIO_DUMP_LEN        (CC_RADIO_DUMP_CONFIG_LEN + CC_RADIO_DUMP_STATUS_LEN)

/*!
 * \brief Initialize SPI2, deselect the ICU-20201, and add + reset the CC1101. Call once at boot.
 * Owns spi_bus_initialize(); an app that also runs the SonicLib BSP must instead add the CC1101 to
 * the bus the BSP brought up.
 */
esp_err_t cc_radio_init(void);

/*! \brief The CC1101 handle. Requires cc_radio_init(). */
cc1101_t *cc_radio_dev(void);

/*!
 * \brief Strobe IDLE and write a complete register set (0x00-0x2E, one burst) and PATABLE[0..1],
 * e.g. a cc_cfg_t's tx_regs or rx_regs (cc_cfg_select.h). The data rate comes from \a regs.
 * Leaves the radio in IDLE. Requires cc_radio_init().
 */
esp_err_t cc_radio_load(const uint8_t regs[CC_RADIO_DUMP_CONFIG_LEN], const uint8_t patable[2]);

/*! \brief Read back the data rate the CC1101 is running at, in Baud. Requires cc_radio_init(). */
esp_err_t cc_radio_get_datarate(uint32_t *baud);

/*!
 * \brief Set the data rate (MDMCFG4.DRATE_E / MDMCFG3.DRATE_M) to the closest value to \a baud,
 * leaving the channel bandwidth bits of MDMCFG4 untouched. Requires cc_radio_init().
 *
 * \param actual_baud  if non-NULL, receives the rate the CC1101 actually runs at
 */
esp_err_t cc_radio_set_datarate(uint32_t baud, uint32_t *actual_baud);

/*! \brief True if \a addr is writable by cc_radio_write(): 0x00-0x2E, CC_RADIO_ADDR_PA_LOW or CC_RADIO_ADDR_PA_HIGH. */
bool cc_radio_addr_writable(uint8_t addr);

/*!
 * \brief Write one register: 0x00-0x2E is a config register; CC_RADIO_ADDR_PA_LOW (0x3E) and
 * CC_RADIO_ADDR_PA_HIGH (0x3F) set PATABLE[0] and PATABLE[1] (the ASK "0" and "1" levels).
 * Requires cc_radio_init().
 */
esp_err_t cc_radio_write(uint8_t addr, uint8_t value);

/*! \brief Read the same address space as cc_radio_write(). Requires cc_radio_init(). */
esp_err_t cc_radio_read(uint8_t addr, uint8_t *value);

/*! \brief Read the register dump into \a out. Requires cc_radio_init(). */
esp_err_t cc_radio_dump_pack(uint8_t out[CC_RADIO_DUMP_LEN]);

/*! \brief Print a packed register dump (from this node or a remote one) as a hex table. */
void cc_radio_dump_print_packed(const uint8_t dump[CC_RADIO_DUMP_LEN]);

/*! \brief Read and print this node's register dump and PATABLE. Requires cc_radio_init(). */
esp_err_t cc_radio_dump_print(void);

#ifdef __cplusplus
}
#endif

#endif /* CC_RADIO_H_ */
