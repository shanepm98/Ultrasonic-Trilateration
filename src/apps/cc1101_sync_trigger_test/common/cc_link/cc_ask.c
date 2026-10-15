/*! \file cc_ask.c
 *
 * \brief Implementation of the async-ASK CC1101 configuration. See cc_ask.h.
 */

#include "cc_ask.h"

#include <inttypes.h>
#include <math.h>
#include <stdio.h>

#include "driver/gpio.h"
#include "driver/spi_master.h"

#define CC_ASK_SPI_HOST SPI2_HOST
/* Same pins as BSP_PIN_SPI_* / BSP_PIN_SPI_CS in components/soniclib_esp32_bsp/esp32_bsp_internal.h
 * (private to the BSP, so restated here) - see CAD/Rangefinder_test_schematic_v3 */
#define CC_ASK_PIN_MOSI   GPIO_NUM_23
#define CC_ASK_PIN_MISO   GPIO_NUM_19
#define CC_ASK_PIN_SCLK   GPIO_NUM_18
#define CC_ASK_PIN_ICU_CS GPIO_NUM_5

#define CC_ASK_XOSC_HZ 26000000.0 /* crystal on the CC1101 modules */

static cc1101_t cc_dev;

/* Register set for 0x00-0x2E, written in one burst. 26 MHz crystal, 433.92 MHz, ASK, async
 * serial mode. MDMCFG4's low nibble and MDMCFG3 (the data rate) are overwritten by
 * cc_ask_set_datarate(). */
static const uint8_t cc_ask_regs[CC_ASK_DUMP_CONFIG_LEN] = {
		[CC1101_IOCFG2]   = 0x0D, /* GDO2: serial data output (async) -> oscilloscope */
		[CC1101_IOCFG1]   = 0x2E, /* GDO1 (= SO/MISO): high-Z when CSn is high - shared bus */
		[CC1101_IOCFG0]   = 0x2E, /* GDO0: output high-Z; it is the async TX data input */
		[CC1101_FIFOTHR]  = 0x47, /* reset default (FIFOs unused in async mode) */
		[CC1101_SYNC1]    = 0xD3, /* reset default (unused - no sync word) */
		[CC1101_SYNC0]    = 0x91,
		[CC1101_PKTLEN]   = 0xFF,
		[CC1101_PKTCTRL1] = 0x00, /* no status append, no address check */
		[CC1101_PKTCTRL0] = 0x32, /* PKT_FORMAT = 3 (async serial), no CRC, infinite length */
		[CC1101_ADDR]     = 0x00,
		[CC1101_CHANNR]   = 0x00,
		[CC1101_FSCTRL1]  = 0x06, /* IF = 152 kHz (SmartRF Studio) */
		[CC1101_FSCTRL0]  = 0x00,
		[CC1101_FREQ2]    = 0x10, /* 433.92 MHz = 0x10B071 * 26 MHz / 2^16 */
		[CC1101_FREQ1]    = 0xB0,
		[CC1101_FREQ0]    = 0x71,
		[CC1101_MDMCFG4]  = 0x50, /* CHANBW_E = 1, CHANBW_M = 1: RX filter 325 kHz; DRATE_E set later */
		[CC1101_MDMCFG3]  = 0x00, /* DRATE_M set later */
		[CC1101_MDMCFG2]  = 0x30, /* ASK/OOK, Manchester off, SYNC_MODE = 0 (no preamble/sync) */
		[CC1101_MDMCFG1]  = 0x02, /* reset default */
		[CC1101_MDMCFG0]  = 0xF8, /* reset default */
		[CC1101_DEVIATN]  = 0x47, /* reset default (FSK only) */
		[CC1101_MCSM2]    = 0x07,
		[CC1101_MCSM1]    = 0x30, /* reset default: RXOFF/TXOFF -> IDLE */
		[CC1101_MCSM0]    = 0x18, /* FS_AUTOCAL = 1: calibrate on IDLE -> RX/TX; PO_TIMEOUT 149 us */
		[CC1101_FOCCFG]   = 0x16,
		[CC1101_BSCFG]    = 0x6C,
		[CC1101_AGCCTRL2] = 0x03, /* DN022 (OOK): 0x03-0x07; tune at runtime for ASK */
		[CC1101_AGCCTRL1] = 0x00, /* DN022 (OOK) */
		[CC1101_AGCCTRL0] = 0x91, /* DN022 (OOK): 0x91 or 0x92 */
		[CC1101_WOREVT1]  = 0x87,
		[CC1101_WOREVT0]  = 0x6B,
		[CC1101_WORCTRL]  = 0xFB,
		[CC1101_FREND1]   = 0x56,
		[CC1101_FREND0]   = 0x11, /* PA_POWER = 1: ASK switches between PATABLE[0] and [1] */
		[CC1101_FSCAL3]   = 0xE9, /* FSCAL3..0, TEST2..0: SmartRF Studio values */
		[CC1101_FSCAL2]   = 0x2A,
		[CC1101_FSCAL1]   = 0x00,
		[CC1101_FSCAL0]   = 0x1F,
		[CC1101_RCCTRL1]  = 0x41,
		[CC1101_RCCTRL0]  = 0x00,
		[CC1101_FSTEST]   = 0x59,
		[CC1101_PTEST]    = 0x7F,
		[CC1101_AGCTEST]  = 0x3F,
		[CC1101_TEST2]    = 0x81,
		[CC1101_TEST1]    = 0x35,
		[CC1101_TEST0]    = 0x09,
};

static uint8_t cc_ask_patable[2] = {CC_ASK_PA_LOW, CC_ASK_PA_HIGH}; /* ASK "0" level, "1" level */

esp_err_t cc_ask_init(void) {
	/* Hold the ICU-20201 deselected: this app never talks to it, but it sits on the same bus. */
	gpio_config_t icu_cs_cfg = {
			.pin_bit_mask = (1ULL << CC_ASK_PIN_ICU_CS),
			.mode         = GPIO_MODE_OUTPUT,
			.pull_up_en   = GPIO_PULLUP_DISABLE,
			.pull_down_en = GPIO_PULLDOWN_DISABLE,
			.intr_type    = GPIO_INTR_DISABLE,
	};
	esp_err_t err = gpio_config(&icu_cs_cfg);
	if (err != ESP_OK) return err;
	gpio_set_level(CC_ASK_PIN_ICU_CS, 1);

	spi_bus_config_t buscfg = {
			.mosi_io_num     = CC_ASK_PIN_MOSI,
			.miso_io_num     = CC_ASK_PIN_MISO,
			.sclk_io_num     = CC_ASK_PIN_SCLK,
			.quadwp_io_num   = -1,
			.quadhd_io_num   = -1,
			.max_transfer_sz = 64,
	};
	err = spi_bus_initialize(CC_ASK_SPI_HOST, &buscfg, SPI_DMA_CH_AUTO);
	if (err != ESP_OK) return err;

	return cc1101_init(CC_ASK_SPI_HOST, CC_ASK_CS_GPIO, CC_ASK_PIN_MISO, &cc_dev);
}

cc1101_t *cc_ask_dev(void) {
	return &cc_dev;
}

esp_err_t cc_ask_configure(uint32_t baud) {
	esp_err_t err = cc1101_strobe(&cc_dev, CC1101_SIDLE);
	if (err != ESP_OK) return err;
	err = cc1101_write_burst(&cc_dev, 0x00, cc_ask_regs, sizeof(cc_ask_regs));
	if (err != ESP_OK) return err;
	err = cc1101_write_patable(&cc_dev, cc_ask_patable, sizeof(cc_ask_patable));
	if (err != ESP_OK) return err;
	return cc_ask_set_datarate(baud, NULL);
}

esp_err_t cc_ask_set_datarate(uint32_t baud, uint32_t *actual_baud) {
	/* R_DATA = (256 + DRATE_M) * 2^DRATE_E * f_XOSC / 2^28 - datasheet section 12 */
	double target = (double)baud;
	int    e      = (int)floor(log2(target * (1 << 20) / CC_ASK_XOSC_HZ));
	int    m      = (int)lround(target * 268435456.0 / (CC_ASK_XOSC_HZ * ldexp(1.0, e)) - 256.0);
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
		*actual_baud = (uint32_t)lround((256.0 + m) * ldexp(1.0, e) * CC_ASK_XOSC_HZ / 268435456.0);
	}
	return ESP_OK;
}

bool cc_ask_addr_writable(uint8_t addr) {
	return addr <= CC1101_LAST_CONFIG_REG || addr == CC_ASK_ADDR_PA_LOW || addr == CC_ASK_ADDR_PA_HIGH;
}

esp_err_t cc_ask_write(uint8_t addr, uint8_t value) {
	if (addr == CC_ASK_ADDR_PA_LOW || addr == CC_ASK_ADDR_PA_HIGH) {
		cc_ask_patable[addr - CC_ASK_ADDR_PA_LOW] = value;
		return cc1101_write_patable(&cc_dev, cc_ask_patable, sizeof(cc_ask_patable));
	}
	if (addr > CC1101_LAST_CONFIG_REG) return ESP_ERR_INVALID_ARG;
	return cc1101_write_reg(&cc_dev, addr, value);
}

esp_err_t cc_ask_read(uint8_t addr, uint8_t *value) {
	if (addr == CC_ASK_ADDR_PA_LOW || addr == CC_ASK_ADDR_PA_HIGH) {
		uint8_t pa[2];
		esp_err_t err = cc1101_read_patable(&cc_dev, pa, sizeof(pa));
		if (err == ESP_OK) *value = pa[addr - CC_ASK_ADDR_PA_LOW];
		return err;
	}
	if (addr > CC1101_LAST_CONFIG_REG) return ESP_ERR_INVALID_ARG;
	return cc1101_read_reg(&cc_dev, addr, value);
}

esp_err_t cc_ask_dump_pack(uint8_t out[CC_ASK_DUMP_LEN]) {
	for (uint8_t a = 0; a <= CC1101_LAST_CONFIG_REG; a++) {
		esp_err_t err = cc1101_read_reg(&cc_dev, a, &out[a]);
		if (err != ESP_OK) return err;
	}
	for (uint8_t a = CC1101_FIRST_STATUS_REG; a <= CC1101_LAST_STATUS_REG; a++) {
		esp_err_t err = cc1101_read_status(&cc_dev, a, &out[CC_ASK_DUMP_CONFIG_LEN + a - CC1101_FIRST_STATUS_REG]);
		if (err != ESP_OK) return err;
	}
	return ESP_OK;
}

void cc_ask_dump_print_packed(const uint8_t dump[CC_ASK_DUMP_LEN]) {
	printf("  config   +0 +1 +2 +3 +4 +5 +6 +7 +8 +9 +A +B +C +D +E +F\n");
	for (int row = 0; row < CC_ASK_DUMP_CONFIG_LEN; row += 16) {
		printf("  0x%02X:  ", row);
		for (int i = row; i < row + 16 && i < CC_ASK_DUMP_CONFIG_LEN; i++) {
			printf(" %02X", dump[i]);
		}
		printf("\n");
	}
	printf("  status (burst-read headers 0xF0-0xFD = registers 0x30-0x3D)\n");
	printf("  0xF0:  ");
	for (int i = 0; i < CC_ASK_DUMP_STATUS_LEN; i++) {
		printf(" %02X", dump[CC_ASK_DUMP_CONFIG_LEN + i]);
	}
	printf("\n");
}

esp_err_t cc_ask_dump_print(void) {
	uint8_t   dump[CC_ASK_DUMP_LEN];
	esp_err_t err = cc_ask_dump_pack(dump);
	if (err != ESP_OK) return err;
	cc_ask_dump_print_packed(dump);

	uint8_t pa[2];
	err = cc1101_read_patable(&cc_dev, pa, sizeof(pa));
	if (err != ESP_OK) return err;
	printf("  PATABLE[0..1]: %02X %02X\n", pa[0], pa[1]);
	return ESP_OK;
}
