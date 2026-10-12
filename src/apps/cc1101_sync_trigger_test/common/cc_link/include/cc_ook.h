/*! \file cc_ook.h
 *
 * \brief The node's CC1101, configured for 433.92 MHz asynchronous-serial OOK.
 *
 * Asynchronous serial mode (PKTCTRL0.PKT_FORMAT = 3) bypasses the packet handler and FIFOs: in TX
 * the modulator keys the carrier directly from the level on GDO0 (ESP32 GPIO27), and in RX the
 * demodulated data comes straight out on GDO2 (IOCFG2 = 0x0D) for the oscilloscope.
 *
 * The CC1101 shares SPI2 (MOSI 23 / MISO 19 / SCLK 18) with the ICU-20201, whose chip select
 * (GPIO5) is held high here so the sensor stays off the bus.
 */

#ifndef CC_OOK_H_
#define CC_OOK_H_

#include <stdint.h>

#include "cc1101.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CC_OOK_CS_GPIO   GPIO_NUM_25 /* CC1101 CSn */
#define CC_OOK_GDO0_GPIO GPIO_NUM_27 /* CC1101 GDO0 - async TX data input */

#define CC_OOK_MIN_BAUD     600u
#define CC_OOK_MAX_BAUD     250000u /* CC1101 OOK maximum - datasheet Table 3 */
#define CC_OOK_DEFAULT_BAUD 10000u

#define CC_OOK_PA_ON 0xC0 /* PATABLE[1], the OOK "1" level: about +10 dBm at 433 MHz (DN013) */

/*! \brief Register dump: config 0x00-0x2E, then status 0x30-0x3D (read as 0xF0-0xFD). */
#define CC_OOK_DUMP_CONFIG_LEN (CC1101_LAST_CONFIG_REG + 1)
#define CC_OOK_DUMP_STATUS_LEN (CC1101_LAST_STATUS_REG - CC1101_FIRST_STATUS_REG + 1)
#define CC_OOK_DUMP_LEN        (CC_OOK_DUMP_CONFIG_LEN + CC_OOK_DUMP_STATUS_LEN)

/*!
 * \brief Initialize SPI2, deselect the ICU-20201, and add + reset the CC1101. Call once at boot.
 * Owns spi_bus_initialize(); an app that also runs the SonicLib BSP must instead add the CC1101 to
 * the bus the BSP brought up.
 */
esp_err_t cc_ook_init(void);

/*! \brief The CC1101 handle. Requires cc_ook_init(). */
cc1101_t *cc_ook_dev(void);

/*!
 * \brief Strobe IDLE and write the full async-OOK register set, the PATABLE, and a data rate of
 * \a baud. Leaves the radio in IDLE. Requires cc_ook_init().
 */
esp_err_t cc_ook_configure(uint32_t baud);

/*!
 * \brief Set the data rate (MDMCFG4.DRATE_E / MDMCFG3.DRATE_M) to the closest value to \a baud,
 * leaving the channel bandwidth bits of MDMCFG4 untouched. Requires cc_ook_init().
 *
 * \param actual_baud  if non-NULL, receives the rate the CC1101 actually runs at
 */
esp_err_t cc_ook_set_datarate(uint32_t baud, uint32_t *actual_baud);

/*!
 * \brief Write one register: 0x00-0x2E is a config register; CC1101_PATABLE (0x3E) sets the OOK
 * "on" power, PATABLE[1] (PATABLE[0] stays 0x00, the "off" level). Requires cc_ook_init().
 */
esp_err_t cc_ook_write(uint8_t addr, uint8_t value);

/*! \brief Read the same address space as cc_ook_write(). Requires cc_ook_init(). */
esp_err_t cc_ook_read(uint8_t addr, uint8_t *value);

/*! \brief Read the register dump into \a out. Requires cc_ook_init(). */
esp_err_t cc_ook_dump_pack(uint8_t out[CC_OOK_DUMP_LEN]);

/*! \brief Print a packed register dump (from this node or a remote one) as a hex table. */
void cc_ook_dump_print_packed(const uint8_t dump[CC_OOK_DUMP_LEN]);

/*! \brief Read and print this node's register dump and PATABLE. Requires cc_ook_init(). */
esp_err_t cc_ook_dump_print(void);

#ifdef __cplusplus
}
#endif

#endif /* CC_OOK_H_ */
