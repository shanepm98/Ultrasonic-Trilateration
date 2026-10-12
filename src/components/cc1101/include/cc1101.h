/*! \file cc1101.h
 *
 * \brief Minimal SPI driver for the TI CC1101 sub-GHz transceiver.
 *
 * Register access follows the CC1101 datasheet (SWRS061) section 10: the header byte is
 * R/W (bit 7) | burst (bit 6) | address (bits 5:0). Configuration registers 0x00-0x2E are read
 * with header 0x80|addr; status registers 0x30-0x3D share addresses with the command strobes and
 * are read with the burst bit set, i.e. header 0xC0|addr (0xF0-0xFD).
 *
 * Chip select is driven manually: after CSn goes low the CC1101 holds SO (MISO) high until its
 * crystal is running and it is ready to accept a header byte, so every access polls the MISO pin
 * before clocking.
 */

#ifndef CC1101_H_
#define CC1101_H_

#include <stddef.h>
#include <stdint.h>

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Configuration registers (datasheet Table 43) */
#define CC1101_IOCFG2   0x00
#define CC1101_IOCFG1   0x01
#define CC1101_IOCFG0   0x02
#define CC1101_FIFOTHR  0x03
#define CC1101_SYNC1    0x04
#define CC1101_SYNC0    0x05
#define CC1101_PKTLEN   0x06
#define CC1101_PKTCTRL1 0x07
#define CC1101_PKTCTRL0 0x08
#define CC1101_ADDR     0x09
#define CC1101_CHANNR   0x0A
#define CC1101_FSCTRL1  0x0B
#define CC1101_FSCTRL0  0x0C
#define CC1101_FREQ2    0x0D
#define CC1101_FREQ1    0x0E
#define CC1101_FREQ0    0x0F
#define CC1101_MDMCFG4  0x10
#define CC1101_MDMCFG3  0x11
#define CC1101_MDMCFG2  0x12
#define CC1101_MDMCFG1  0x13
#define CC1101_MDMCFG0  0x14
#define CC1101_DEVIATN  0x15
#define CC1101_MCSM2    0x16
#define CC1101_MCSM1    0x17
#define CC1101_MCSM0    0x18
#define CC1101_FOCCFG   0x19
#define CC1101_BSCFG    0x1A
#define CC1101_AGCCTRL2 0x1B
#define CC1101_AGCCTRL1 0x1C
#define CC1101_AGCCTRL0 0x1D
#define CC1101_WOREVT1  0x1E
#define CC1101_WOREVT0  0x1F
#define CC1101_WORCTRL  0x20
#define CC1101_FREND1   0x21
#define CC1101_FREND0   0x22
#define CC1101_FSCAL3   0x23
#define CC1101_FSCAL2   0x24
#define CC1101_FSCAL1   0x25
#define CC1101_FSCAL0   0x26
#define CC1101_RCCTRL1  0x27
#define CC1101_RCCTRL0  0x28
#define CC1101_FSTEST   0x29
#define CC1101_PTEST    0x2A
#define CC1101_AGCTEST  0x2B
#define CC1101_TEST2    0x2C
#define CC1101_TEST1    0x2D
#define CC1101_TEST0    0x2E
#define CC1101_LAST_CONFIG_REG CC1101_TEST0

/* Command strobes (datasheet Table 42) */
#define CC1101_SRES    0x30
#define CC1101_SFSTXON 0x31
#define CC1101_SXOFF   0x32
#define CC1101_SCAL    0x33
#define CC1101_SRX     0x34
#define CC1101_STX     0x35
#define CC1101_SIDLE   0x36
#define CC1101_SWOR    0x38
#define CC1101_SPWD    0x39
#define CC1101_SFRX    0x3A
#define CC1101_SFTX    0x3B
#define CC1101_SWORRST 0x3C
#define CC1101_SNOP    0x3D

/* Status registers (datasheet Table 44) - read with cc1101_read_status() */
#define CC1101_PARTNUM        0x30
#define CC1101_VERSION        0x31
#define CC1101_FREQEST        0x32
#define CC1101_LQI            0x33
#define CC1101_RSSI           0x34
#define CC1101_MARCSTATE      0x35
#define CC1101_WORTIME1       0x36
#define CC1101_WORTIME0       0x37
#define CC1101_PKTSTATUS      0x38
#define CC1101_VCO_VC_DAC     0x39
#define CC1101_TXBYTES        0x3A
#define CC1101_RXBYTES        0x3B
#define CC1101_RCCTRL1_STATUS 0x3C
#define CC1101_RCCTRL0_STATUS 0x3D
#define CC1101_FIRST_STATUS_REG CC1101_PARTNUM
#define CC1101_LAST_STATUS_REG  CC1101_RCCTRL0_STATUS

#define CC1101_PATABLE 0x3E

/* MARCSTATE values (datasheet Table 48) */
#define CC1101_MARCSTATE_IDLE 0x01
#define CC1101_MARCSTATE_RX   0x0D
#define CC1101_MARCSTATE_TX   0x13

/*! \brief One CC1101 on a shared SPI bus. Filled in by cc1101_init(). */
typedef struct {
	spi_device_handle_t spi;
	gpio_num_t          cs;   /*!< CSn, driven manually */
	gpio_num_t          miso; /*!< bus MISO pin - polled for the chip-ready (SO low) condition */
} cc1101_t;

/*!
 * \brief Add the CC1101 to \a host and reset it (datasheet section 19.1.2 manual reset sequence),
 * leaving every register at its reset default.
 *
 * Requires spi_bus_initialize(\a host, ...) first - the bus is shared with other devices (the
 * ICU-20201), so the caller owns it. \a miso must be the bus's MISO pin.
 *
 * \return ESP_OK, ESP_ERR_TIMEOUT if the chip never signals ready, or the SPI/GPIO error
 */
esp_err_t cc1101_init(spi_host_device_t host, gpio_num_t cs, gpio_num_t miso, cc1101_t *dev);

/*! \brief Write one configuration register (0x00-0x2E). Requires cc1101_init(). */
esp_err_t cc1101_write_reg(cc1101_t *dev, uint8_t addr, uint8_t value);

/*! \brief Read one configuration register (0x00-0x2E). Requires cc1101_init(). */
esp_err_t cc1101_read_reg(cc1101_t *dev, uint8_t addr, uint8_t *value);

/*! \brief Read one status register (0x30-0x3D; sent as header 0xF0-0xFD). Requires cc1101_init(). */
esp_err_t cc1101_read_status(cc1101_t *dev, uint8_t addr, uint8_t *value);

/*! \brief Burst-write \a len consecutive configuration registers starting at \a addr. Requires cc1101_init(). */
esp_err_t cc1101_write_burst(cc1101_t *dev, uint8_t addr, const uint8_t *values, size_t len);

/*! \brief Issue a command strobe (0x30-0x3D). Requires cc1101_init(). */
esp_err_t cc1101_strobe(cc1101_t *dev, uint8_t strobe);

/*! \brief Write the first \a len (1..8) PATABLE entries. Requires cc1101_init(). */
esp_err_t cc1101_write_patable(cc1101_t *dev, const uint8_t *values, size_t len);

/*! \brief Read the first \a len (1..8) PATABLE entries. Requires cc1101_init(). */
esp_err_t cc1101_read_patable(cc1101_t *dev, uint8_t *values, size_t len);

/*!
 * \brief Poll MARCSTATE until it reads \a state or \a timeout_us elapses. Requires cc1101_init().
 *
 * \return ESP_OK, ESP_ERR_TIMEOUT, or the SPI error
 */
esp_err_t cc1101_wait_marcstate(cc1101_t *dev, uint8_t state, uint32_t timeout_us);

#ifdef __cplusplus
}
#endif

#endif /* CC1101_H_ */
