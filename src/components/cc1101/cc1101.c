/*! \file cc1101.c
 *
 * \brief Implementation of the CC1101 SPI driver. See cc1101.h.
 *
 * Every access is one CSn-low window: acquire the shared bus (so no other device's transaction can
 * run while this chip select is low), drive CSn low, wait for the chip-ready condition (SO low),
 * clock the header + data in one polling transaction, then release CSn and the bus.
 */

#include "cc1101.h"

#include <string.h>

#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "esp_timer.h"

#define CC1101_SPI_CLOCK_HZ    (1 * 1000 * 1000) /* well under the 6.5 MHz burst-access limit */
#define CC1101_READY_TIMEOUT_US 1000             /* crystal start-up is ~150 us worst case */

#define CC1101_HDR_READ  0x80
#define CC1101_HDR_BURST 0x40

#define CC1101_MAX_BURST (CC1101_LAST_CONFIG_REG + 1)

/* Wait for SO (MISO) low: the CC1101's chip-ready signal while CSn is low. */
static esp_err_t wait_chip_ready(cc1101_t *dev) {
	int64_t start = esp_timer_get_time();
	while (gpio_get_level(dev->miso)) {
		if (esp_timer_get_time() - start > CC1101_READY_TIMEOUT_US) {
			return ESP_ERR_TIMEOUT;
		}
	}
	return ESP_OK;
}

/* One CSn-low access: send tx[0..len), receive into rx[0..len) (rx may be NULL). */
static esp_err_t xfer(cc1101_t *dev, const uint8_t *tx, uint8_t *rx, size_t len) {
	esp_err_t err = spi_device_acquire_bus(dev->spi, portMAX_DELAY);
	if (err != ESP_OK) return err;

	gpio_set_level(dev->cs, 0);
	err = wait_chip_ready(dev);
	if (err == ESP_OK) {
		spi_transaction_t t = {
				.length    = 8 * len,
				.tx_buffer = tx,
				.rx_buffer = rx,
		};
		err = spi_device_polling_transmit(dev->spi, &t);
	}
	gpio_set_level(dev->cs, 1);

	spi_device_release_bus(dev->spi);
	return err;
}

esp_err_t cc1101_init(spi_host_device_t host, gpio_num_t cs, gpio_num_t miso, cc1101_t *dev) {
	dev->cs   = cs;
	dev->miso = miso;

	gpio_config_t cs_cfg = {
			.pin_bit_mask = (1ULL << cs),
			.mode         = GPIO_MODE_OUTPUT,
			.pull_up_en   = GPIO_PULLUP_DISABLE,
			.pull_down_en = GPIO_PULLDOWN_DISABLE,
			.intr_type    = GPIO_INTR_DISABLE,
	};
	esp_err_t err = gpio_config(&cs_cfg);
	if (err != ESP_OK) return err;
	gpio_set_level(cs, 1); /* deselected */

	spi_device_interface_config_t devcfg = {
			.mode           = 0, /* CC1101 SPI: CPOL=0, CPHA=0 - datasheet section 10 */
			.clock_speed_hz = CC1101_SPI_CLOCK_HZ,
			.spics_io_num   = -1, /* CSn driven manually so SO can be polled before clocking */
			.queue_size     = 1,
	};
	err = spi_bus_add_device(host, &devcfg, &dev->spi);
	if (err != ESP_OK) return err;

	/* Manual power-on reset - datasheet section 19.1.2: strobe CSn low/high, hold CSn high for at
	 * least 40 us, then pull CSn low, wait for SO low, issue SRES, and wait for SO low again (reset
	 * complete). The whole sequence holds the bus. */
	err = spi_device_acquire_bus(dev->spi, portMAX_DELAY);
	if (err != ESP_OK) return err;

	gpio_set_level(cs, 0);
	esp_rom_delay_us(10);
	gpio_set_level(cs, 1);
	esp_rom_delay_us(50);
	gpio_set_level(cs, 0);
	err = wait_chip_ready(dev);
	if (err == ESP_OK) {
		uint8_t           sres = CC1101_SRES;
		spi_transaction_t t    = {
				   .length    = 8,
				   .tx_buffer = &sres,
		};
		err = spi_device_polling_transmit(dev->spi, &t);
	}
	if (err == ESP_OK) {
		err = wait_chip_ready(dev);
	}
	gpio_set_level(cs, 1);

	spi_device_release_bus(dev->spi);
	return err;
}

esp_err_t cc1101_write_reg(cc1101_t *dev, uint8_t addr, uint8_t value) {
	uint8_t tx[2] = {addr, value};
	return xfer(dev, tx, NULL, sizeof(tx));
}

esp_err_t cc1101_read_reg(cc1101_t *dev, uint8_t addr, uint8_t *value) {
	uint8_t tx[2] = {CC1101_HDR_READ | addr, 0};
	uint8_t rx[2];
	esp_err_t err = xfer(dev, tx, rx, sizeof(tx));
	if (err == ESP_OK) *value = rx[1];
	return err;
}

esp_err_t cc1101_read_status(cc1101_t *dev, uint8_t addr, uint8_t *value) {
	return cc1101_read_reg(dev, CC1101_HDR_BURST | addr, value); /* burst bit selects status, not strobe */
}

esp_err_t cc1101_write_burst(cc1101_t *dev, uint8_t addr, const uint8_t *values, size_t len) {
	if (len == 0 || len > CC1101_MAX_BURST) return ESP_ERR_INVALID_ARG;
	uint8_t tx[1 + CC1101_MAX_BURST];
	tx[0] = CC1101_HDR_BURST | addr;
	memcpy(&tx[1], values, len);
	return xfer(dev, tx, NULL, 1 + len);
}

esp_err_t cc1101_strobe(cc1101_t *dev, uint8_t strobe) {
	return xfer(dev, &strobe, NULL, 1);
}

esp_err_t cc1101_write_patable(cc1101_t *dev, const uint8_t *values, size_t len) {
	if (len == 0 || len > 8) return ESP_ERR_INVALID_ARG;
	return cc1101_write_burst(dev, CC1101_PATABLE, values, len);
}

esp_err_t cc1101_read_patable(cc1101_t *dev, uint8_t *values, size_t len) {
	if (len == 0 || len > 8) return ESP_ERR_INVALID_ARG;
	uint8_t tx[1 + 8] = {CC1101_HDR_READ | CC1101_HDR_BURST | CC1101_PATABLE};
	uint8_t rx[1 + 8];
	esp_err_t err = xfer(dev, tx, rx, 1 + len);
	if (err == ESP_OK) memcpy(values, &rx[1], len);
	return err;
}

esp_err_t cc1101_wait_marcstate(cc1101_t *dev, uint8_t state, uint32_t timeout_us) {
	int64_t start = esp_timer_get_time();
	for (;;) {
		uint8_t s;
		esp_err_t err = cc1101_read_status(dev, CC1101_MARCSTATE, &s);
		if (err != ESP_OK) return err;
		if ((s & 0x1F) == state) return ESP_OK;
		if (esp_timer_get_time() - start > timeout_us) return ESP_ERR_TIMEOUT;
	}
}
