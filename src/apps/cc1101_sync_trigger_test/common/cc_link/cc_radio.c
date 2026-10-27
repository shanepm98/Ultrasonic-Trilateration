/*! \file cc_radio.c
 *
 * \brief Implementation of the node's CC1101 setup and register access. See cc_radio.h.
 */

#include "cc_radio.h"

#include <inttypes.h>
#include <math.h>
#include <stdio.h>

#include "driver/gpio.h"
#include "driver/spi_master.h"

#define CC_RADIO_SPI_HOST SPI2_HOST
/* Same pins as BSP_PIN_SPI_* / BSP_PIN_SPI_CS in components/soniclib_esp32_bsp/esp32_bsp_internal.h
 * (private to the BSP, so restated here) - see CAD/Rangefinder_test_schematic_v3 */
#define CC_RADIO_PIN_MOSI   GPIO_NUM_23
#define CC_RADIO_PIN_MISO   GPIO_NUM_19
#define CC_RADIO_PIN_SCLK   GPIO_NUM_18
#define CC_RADIO_PIN_ICU_CS GPIO_NUM_5

#define CC_RADIO_XOSC_HZ 26000000.0 /* crystal on the CC1101 modules */

static cc1101_t cc_dev;

static uint8_t cc_radio_patable[2] = {CC_RADIO_PA_LOW, CC_RADIO_PA_HIGH}; /* mirror of PATABLE[0..1] */

esp_err_t cc_radio_init(void) {
	/* Hold the ICU-20201 deselected: this app never talks to it, but it sits on the same bus. */
	gpio_config_t icu_cs_cfg = {
			.pin_bit_mask = (1ULL << CC_RADIO_PIN_ICU_CS),
			.mode         = GPIO_MODE_OUTPUT,
			.pull_up_en   = GPIO_PULLUP_DISABLE,
			.pull_down_en = GPIO_PULLDOWN_DISABLE,
			.intr_type    = GPIO_INTR_DISABLE,
	};
	esp_err_t err = gpio_config(&icu_cs_cfg);
	if (err != ESP_OK) return err;
	gpio_set_level(CC_RADIO_PIN_ICU_CS, 1);

	spi_bus_config_t buscfg = {
			.mosi_io_num     = CC_RADIO_PIN_MOSI,
			.miso_io_num     = CC_RADIO_PIN_MISO,
			.sclk_io_num     = CC_RADIO_PIN_SCLK,
			.quadwp_io_num   = -1,
			.quadhd_io_num   = -1,
			.max_transfer_sz = 64,
	};
	err = spi_bus_initialize(CC_RADIO_SPI_HOST, &buscfg, SPI_DMA_CH_AUTO);
	if (err != ESP_OK) return err;

	return cc1101_init(CC_RADIO_SPI_HOST, CC_RADIO_CS_GPIO, CC_RADIO_PIN_MISO, &cc_dev);
}

cc1101_t *cc_radio_dev(void) {
	return &cc_dev;
}

esp_err_t cc_radio_load(const uint8_t regs[CC_RADIO_DUMP_CONFIG_LEN], const uint8_t patable[2]) {
	esp_err_t err = cc1101_strobe(&cc_dev, CC1101_SIDLE);
	if (err != ESP_OK) return err;
	err = cc1101_write_burst(&cc_dev, 0x00, regs, CC_RADIO_DUMP_CONFIG_LEN);
	if (err != ESP_OK) return err;
	cc_radio_patable[0] = patable[0];
	cc_radio_patable[1] = patable[1];
	return cc1101_write_patable(&cc_dev, cc_radio_patable, sizeof(cc_radio_patable));
}

esp_err_t cc_radio_get_datarate(uint32_t *baud) {
	uint8_t mdmcfg4, mdmcfg3;
	esp_err_t err = cc1101_read_reg(&cc_dev, CC1101_MDMCFG4, &mdmcfg4);
	if (err == ESP_OK) err = cc1101_read_reg(&cc_dev, CC1101_MDMCFG3, &mdmcfg3);
	if (err != ESP_OK) return err;
	*baud = (uint32_t)lround((256.0 + mdmcfg3) * ldexp(1.0, mdmcfg4 & 0x0F) * CC_RADIO_XOSC_HZ / 268435456.0);
	return ESP_OK;
}

esp_err_t cc_radio_set_datarate(uint32_t baud, uint32_t *actual_baud) {
	/* R_DATA = (256 + DRATE_M) * 2^DRATE_E * f_XOSC / 2^28 - datasheet section 12 */
	double target = (double)baud;
	int    e      = (int)floor(log2(target * (1 << 20) / CC_RADIO_XOSC_HZ));
	int    m      = (int)lround(target * 268435456.0 / (CC_RADIO_XOSC_HZ * ldexp(1.0, e)) - 256.0);
	if (m > 255) {
		m = 0;
		e++;
	}
	if (e < 0) e = 0;
	if (e > 15) e = 15;

	uint8_t mdmcfg4;
	esp_err_t err = cc1101_read_reg(&cc_dev, CC1101_MDMCFG4, &mdmcfg4);
	if (err != ESP_OK) return err;
	err = cc1101_write_reg(&cc_dev, CC1101_MDMCFG4, (uint8_t)((mdmcfg4 & 0xF0) | e));
	if (err != ESP_OK) return err;
	err = cc1101_write_reg(&cc_dev, CC1101_MDMCFG3, (uint8_t)m);
	if (err != ESP_OK) return err;

	if (actual_baud) {
		*actual_baud = (uint32_t)lround((256.0 + m) * ldexp(1.0, e) * CC_RADIO_XOSC_HZ / 268435456.0);
	}
	return ESP_OK;
}

bool cc_radio_addr_writable(uint8_t addr) {
	return addr <= CC1101_LAST_CONFIG_REG || addr == CC_RADIO_ADDR_PA_LOW || addr == CC_RADIO_ADDR_PA_HIGH;
}

esp_err_t cc_radio_write(uint8_t addr, uint8_t value) {
	if (addr == CC_RADIO_ADDR_PA_LOW || addr == CC_RADIO_ADDR_PA_HIGH) {
		cc_radio_patable[addr - CC_RADIO_ADDR_PA_LOW] = value;
		return cc1101_write_patable(&cc_dev, cc_radio_patable, sizeof(cc_radio_patable));
	}
	if (addr > CC1101_LAST_CONFIG_REG) return ESP_ERR_INVALID_ARG;
	return cc1101_write_reg(&cc_dev, addr, value);
}

esp_err_t cc_radio_read(uint8_t addr, uint8_t *value) {
	if (addr == CC_RADIO_ADDR_PA_LOW || addr == CC_RADIO_ADDR_PA_HIGH) {
		uint8_t pa[2];
		esp_err_t err = cc1101_read_patable(&cc_dev, pa, sizeof(pa));
		if (err == ESP_OK) *value = pa[addr - CC_RADIO_ADDR_PA_LOW];
		return err;
	}
	if (addr > CC1101_LAST_CONFIG_REG) return ESP_ERR_INVALID_ARG;
	return cc1101_read_reg(&cc_dev, addr, value);
}

esp_err_t cc_radio_dump_pack(uint8_t out[CC_RADIO_DUMP_LEN]) {
	for (uint8_t a = 0; a <= CC1101_LAST_CONFIG_REG; a++) {
		esp_err_t err = cc1101_read_reg(&cc_dev, a, &out[a]);
		if (err != ESP_OK) return err;
	}
	for (uint8_t a = CC1101_FIRST_STATUS_REG; a <= CC1101_LAST_STATUS_REG; a++) {
		esp_err_t err = cc1101_read_status(&cc_dev, a, &out[CC_RADIO_DUMP_CONFIG_LEN + a - CC1101_FIRST_STATUS_REG]);
		if (err != ESP_OK) return err;
	}
	return ESP_OK;
}

void cc_radio_dump_print_packed(const uint8_t dump[CC_RADIO_DUMP_LEN]) {
	printf("  config   +0 +1 +2 +3 +4 +5 +6 +7 +8 +9 +A +B +C +D +E +F\n");
	for (int row = 0; row < CC_RADIO_DUMP_CONFIG_LEN; row += 16) {
		printf("  0x%02X:  ", row);
		for (int i = row; i < row + 16 && i < CC_RADIO_DUMP_CONFIG_LEN; i++) {
			printf(" %02X", dump[i]);
		}
		printf("\n");
	}
	printf("  status (burst-read headers 0xF0-0xFD = registers 0x30-0x3D)\n");
	printf("  0xF0:  ");
	for (int i = 0; i < CC_RADIO_DUMP_STATUS_LEN; i++) {
		printf(" %02X", dump[CC_RADIO_DUMP_CONFIG_LEN + i]);
	}
	printf("\n");
}

esp_err_t cc_radio_dump_print(void) {
	uint8_t   dump[CC_RADIO_DUMP_LEN];
	esp_err_t err = cc_radio_dump_pack(dump);
	if (err != ESP_OK) return err;
	cc_radio_dump_print_packed(dump);

	uint8_t pa[2];
	err = cc1101_read_patable(&cc_dev, pa, sizeof(pa));
	if (err != ESP_OK) return err;
	printf("  PATABLE[0..1]: %02X %02X\n", pa[0], pa[1]);
	return ESP_OK;
}
