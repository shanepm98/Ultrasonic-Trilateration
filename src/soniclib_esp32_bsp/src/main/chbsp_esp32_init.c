/*! \file chbsp_esp32_init.c
 *
 * \brief One-time hardware initialization for the ESP32 SonicLib BSP. See chbsp_esp32_init.h.
 */

#include "chbsp_esp32_init.h"
#include "esp32_bsp_internal.h"

#include "esp_heap_caps.h"
#include "esp_log.h"

static const char *TAG = "chbsp_esp32_init";

/* Shared BSP state, defined here and used by esp32_bsp.c via esp32_bsp_internal.h */
ch_group_t *bsp_grp_ptr                = NULL;
spi_device_handle_t bsp_spi_handle     = NULL;
uint8_t *bsp_spi_scratch               = NULL;
EventGroupHandle_t bsp_event_group     = NULL;

static esp_err_t init_gpio(void) {
	gpio_config_t int1_cfg = {
			.pin_bit_mask = (1ULL << BSP_PIN_INT1),
			.mode         = GPIO_MODE_INPUT,
			.pull_up_en   = GPIO_PULLUP_DISABLE, /* external 2.2k pull-up already present */
			.pull_down_en = GPIO_PULLDOWN_DISABLE,
			.intr_type    = GPIO_INTR_NEGEDGE, /* ICU interrupt lines are active low */
	};
	esp_err_t err = gpio_config(&int1_cfg);
	if (err != ESP_OK) return err;

	gpio_config_t out_cfg = {
			.pin_bit_mask = (1ULL << BSP_PIN_INT2) | (1ULL << BSP_PIN_SPI_CS),
			.mode         = GPIO_MODE_OUTPUT,
			.pull_up_en   = GPIO_PULLUP_DISABLE,
			.pull_down_en = GPIO_PULLDOWN_DISABLE,
			.intr_type    = GPIO_INTR_DISABLE,
	};
	err = gpio_config(&out_cfg);
	if (err != ESP_OK) return err;

	gpio_set_level(BSP_PIN_INT2, 1);    /* trigger line idle high */
	gpio_set_level(BSP_PIN_SPI_CS, 1);  /* chip select idle high (inactive) */

	err = gpio_install_isr_service(0);
	if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return err; /* already installed is fine */

	err = gpio_isr_handler_add(BSP_PIN_INT1, bsp_int1_isr_handler, NULL);
	if (err != ESP_OK) return err;

	return gpio_intr_disable(BSP_PIN_INT1); /* SonicLib arms it later via chbsp_int1_interrupt_enable() */
}

static esp_err_t init_spi(void) {
	spi_bus_config_t buscfg = {
			.mosi_io_num     = BSP_PIN_SPI_MOSI,
			.miso_io_num     = BSP_PIN_SPI_MISO,
			.sclk_io_num     = BSP_PIN_SPI_SCLK,
			.quadwp_io_num   = -1,
			.quadhd_io_num   = -1,
			.max_transfer_sz = BSP_SPI_SCRATCH_BYTES,
	};
	esp_err_t err = spi_bus_initialize(BSP_SPI_HOST, &buscfg, SPI_DMA_CH_AUTO);
	if (err != ESP_OK) return err;

	spi_device_interface_config_t devcfg = {
			.mode           = 0, /* ICU-20201 SPI: CPOL=0, CPHA=0 */
			.clock_speed_hz = BSP_SPI_CLOCK_HZ,
			.spics_io_num   = -1, /* CS driven manually by chbsp_spi_cs_on()/off() */
			.queue_size     = 4,
	};
	err = spi_bus_add_device(BSP_SPI_HOST, &devcfg, &bsp_spi_handle);
	if (err != ESP_OK) return err;

	bsp_spi_scratch = heap_caps_malloc(BSP_SPI_SCRATCH_BYTES, MALLOC_CAP_DMA);
	if (bsp_spi_scratch == NULL) return ESP_ERR_NO_MEM;

	return ESP_OK;
}

esp_err_t chbsp_esp32_init(ch_group_t *grp_ptr) {
	if (grp_ptr == NULL) return ESP_ERR_INVALID_ARG;
	bsp_grp_ptr = grp_ptr;

	esp_err_t err = init_gpio();
	if (err != ESP_OK) {
		ESP_LOGE(TAG, "GPIO init failed: %s", esp_err_to_name(err));
		return err;
	}

	err = init_spi();
	if (err != ESP_OK) {
		ESP_LOGE(TAG, "SPI init failed: %s", esp_err_to_name(err));
		return err;
	}

	bsp_event_group = xEventGroupCreate();
	if (bsp_event_group == NULL) return ESP_ERR_NO_MEM;

	return ESP_OK;
}
