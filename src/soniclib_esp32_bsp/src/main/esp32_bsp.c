/*! \file esp32_bsp.c
 *
 * \brief ESP32 (esp-idf) board support package for SonicLib, for a single ICU-20201 sensor
 * connected over SPI. See chirp_bsp.h for the function contracts and esp32_bsp_internal.h for
 * pin assignments.
 *
 * All functions here depend on chbsp_esp32_init() (chbsp_esp32_init.c) having already been
 * called by the application.
 */

#include "chirp_bsp.h"
#include "esp32_bsp_internal.h"

#include <string.h>
#include <stdio.h>

#include "esp_rom_sys.h"
#include "esp_timer.h"

/* ===================== INT1 (data-ready interrupt, CHIRP_SENSOR_INT_PIN) ===================== */

void chbsp_group_set_int1_dir_out(ch_group_t *grp_ptr) {
	(void)grp_ptr;
	gpio_set_direction(BSP_PIN_INT1, GPIO_MODE_OUTPUT);
}

void chbsp_set_int1_dir_out(ch_dev_t *dev_ptr) {
	(void)dev_ptr;
	gpio_set_direction(BSP_PIN_INT1, GPIO_MODE_OUTPUT);
}

void chbsp_group_set_int1_dir_in(ch_group_t *grp_ptr) {
	(void)grp_ptr;
	gpio_set_direction(BSP_PIN_INT1, GPIO_MODE_INPUT);
}

void chbsp_set_int1_dir_in(ch_dev_t *dev_ptr) {
	(void)dev_ptr;
	gpio_set_direction(BSP_PIN_INT1, GPIO_MODE_INPUT);
}

void chbsp_group_int1_clear(ch_group_t *grp_ptr) {
	(void)grp_ptr;
	gpio_set_level(BSP_PIN_INT1, 0); /* active low */
}

void chbsp_int1_clear(ch_dev_t *dev_ptr) {
	(void)dev_ptr;
	gpio_set_level(BSP_PIN_INT1, 0);
}

void chbsp_group_int1_set(ch_group_t *grp_ptr) {
	(void)grp_ptr;
	gpio_set_level(BSP_PIN_INT1, 1); /* inactive high */
}

void chbsp_int1_set(ch_dev_t *dev_ptr) {
	(void)dev_ptr;
	gpio_set_level(BSP_PIN_INT1, 1);
}

void chbsp_group_int1_interrupt_enable(ch_group_t *grp_ptr) {
	(void)grp_ptr;
	gpio_intr_enable(BSP_PIN_INT1);
}

void chbsp_int1_interrupt_enable(ch_dev_t *dev_ptr) {
	(void)dev_ptr;
	gpio_intr_enable(BSP_PIN_INT1);
}

void chbsp_group_int1_interrupt_disable(ch_group_t *grp_ptr) {
	(void)grp_ptr;
	gpio_intr_disable(BSP_PIN_INT1);
}

void chbsp_int1_interrupt_disable(ch_dev_t *dev_ptr) {
	(void)dev_ptr;
	gpio_intr_disable(BSP_PIN_INT1);
}

/* ===================== INT2 (hardware trigger, CHIRP_SENSOR_TRIG_PIN) ===================== */

void chbsp_group_set_int2_dir_out(ch_group_t *grp_ptr) {
	(void)grp_ptr;
	gpio_set_direction(BSP_PIN_INT2, GPIO_MODE_OUTPUT);
}

void chbsp_set_int2_dir_out(ch_dev_t *dev_ptr) {
	(void)dev_ptr;
	gpio_set_direction(BSP_PIN_INT2, GPIO_MODE_OUTPUT);
}

void chbsp_group_set_int2_dir_in(ch_group_t *grp_ptr) {
	(void)grp_ptr;
	gpio_set_direction(BSP_PIN_INT2, GPIO_MODE_INPUT);
}

void chbsp_set_int2_dir_in(ch_dev_t *dev_ptr) {
	(void)dev_ptr;
	gpio_set_direction(BSP_PIN_INT2, GPIO_MODE_INPUT);
}

void chbsp_group_int2_clear(ch_group_t *grp_ptr) {
	(void)grp_ptr;
	gpio_set_level(BSP_PIN_INT2, 0); /* active low */
}

void chbsp_int2_clear(ch_dev_t *dev_ptr) {
	(void)dev_ptr;
	gpio_set_level(BSP_PIN_INT2, 0);
}

void chbsp_group_int2_set(ch_group_t *grp_ptr) {
	(void)grp_ptr;
	gpio_set_level(BSP_PIN_INT2, 1); /* inactive high */
}

void chbsp_int2_set(ch_dev_t *dev_ptr) {
	(void)dev_ptr;
	gpio_set_level(BSP_PIN_INT2, 1);
}

/* ===================== SPI ===================== */

void chbsp_spi_cs_on(ch_dev_t *dev_ptr) {
	(void)dev_ptr;
	gpio_set_level(BSP_PIN_SPI_CS, 0);
}

void chbsp_spi_cs_off(ch_dev_t *dev_ptr) {
	(void)dev_ptr;
	gpio_set_level(BSP_PIN_SPI_CS, 1);
}

int chbsp_spi_write(ch_dev_t *dev_ptr, const uint8_t *data, uint16_t num_bytes) {
	(void)dev_ptr;
	if (num_bytes == 0) return 0;
	if (num_bytes > BSP_SPI_SCRATCH_BYTES) return 1;

	/* Copy through a DMA-capable scratch buffer - callers within soniclib do not guarantee their
	 * buffers are DMA-capable, and this bus uses DMA (required for transfers over 64 bytes). */
	memcpy(bsp_spi_scratch, data, num_bytes);

	spi_transaction_t trans = {
			.length    = (size_t)num_bytes * 8,
			.tx_buffer = bsp_spi_scratch,
	};
	esp_err_t err = spi_device_transmit(bsp_spi_handle, &trans);
	return (err == ESP_OK) ? 0 : 1;
}

int chbsp_spi_read(ch_dev_t *dev_ptr, uint8_t *data, uint16_t num_bytes) {
	(void)dev_ptr;
	if (num_bytes == 0) return 0;
	if (num_bytes > BSP_SPI_SCRATCH_BYTES) return 1;

	spi_transaction_t trans = {
			.length    = (size_t)num_bytes * 8,
			.rx_buffer = bsp_spi_scratch,
	};
	esp_err_t err = spi_device_transmit(bsp_spi_handle, &trans);
	if (err != ESP_OK) return 1;

	memcpy(data, bsp_spi_scratch, num_bytes);
	return 0;
}

/* ===================== Delay / timestamp ===================== */

void chbsp_delay_us(uint32_t us) {
	esp_rom_delay_us(us);
}

void chbsp_pulse_len_hint_us(uint32_t us) {
	chbsp_delay_us(us);
}

/* Busy-wait rather than vTaskDelay(): CONFIG_FREERTOS_HZ=100 (10ms tick) is too coarse for the
 * RTC calibration pulse timed by this function, whose accuracy directly affects range accuracy
 * (see chirp_bsp.h). Only used for short one-time delays during ch_group_start(), not in the
 * measurement hot path. */
void chbsp_delay_ms(uint32_t ms) {
	esp_rom_delay_us(ms * 1000);
}

uint32_t chbsp_timestamp_ms(void) {
	return (uint32_t)(esp_timer_get_time() / 1000);
}

/* ===================== Event wait/notify ===================== */

void chbsp_event_wait_setup(uint32_t event_mask) {
	xEventGroupClearBits(bsp_event_group, event_mask);
}

uint8_t chbsp_event_wait(uint16_t time_out_ms, uint32_t event_mask) {
	EventBits_t bits = xEventGroupWaitBits(bsp_event_group, event_mask, pdFALSE, pdTRUE,
	                                       pdMS_TO_TICKS(time_out_ms));
	return ((bits & event_mask) == event_mask) ? 0 : 1;
}

/* Called from ch_interrupt(), in ISR context (see bsp_int1_isr_handler() below). */
void chbsp_event_notify(uint32_t event_mask) {
	BaseType_t higher_priority_task_woken = pdFALSE;
	xEventGroupSetBitsFromISR(bsp_event_group, event_mask, &higher_priority_task_woken);
	portYIELD_FROM_ISR(higher_priority_task_woken);
}

/* ===================== Debug output ===================== */

void chbsp_print_str(const char *str) {
	printf("%s", str);
}

/* ===================== INT1 GPIO ISR ===================== */

void bsp_int1_isr_handler(void *arg) {
	(void)arg;
	ch_interrupt(bsp_grp_ptr, 0); /* single sensor on this board, dev_num is always 0 */
}

