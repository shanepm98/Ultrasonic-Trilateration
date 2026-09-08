/*! \file esp32_bsp_internal.h
 *
 * \brief Pin assignments and shared hardware handles for the ESP32 BSP implementation.
 *
 * Not part of the chirp_bsp.h public interface - shared only between chbsp_esp32_init.c (which
 * owns and initializes this state) and esp32_bsp.c (which uses it to implement the chbsp_ functions).
 */

#ifndef ESP32_BSP_INTERNAL_H_
#define ESP32_BSP_INTERNAL_H_

#include <invn/soniclib/soniclib.h>

/* This BSP implements only the SPI transport (chbsp_spi_*) that ICU / Shasta-generation sensors
 * use - there is no chbsp_i2c_* here. SonicLib picks SPI vs I2C at compile time via these
 * symbols; if INCLUDE_SHASTA_SUPPORT is missing it silently falls back to the CHx01 / I2C path
 * (see invn/soniclib/details/chirp_board_config.h), which would not even link against this BSP.
 * Fail loudly instead. */
#if !defined(INCLUDE_SHASTA_SUPPORT) || defined(INCLUDE_WHITNEY_SUPPORT)
#error "Build SonicLib with INCLUDE_SHASTA_SUPPORT and without INCLUDE_WHITNEY_SUPPORT for this ICU-20201 SPI BSP"
#endif

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Pin assignments - see CAD/Rangefinder_test_schematic_v3 */
#define BSP_PIN_SPI_MOSI GPIO_NUM_23
#define BSP_PIN_SPI_MISO GPIO_NUM_19
#define BSP_PIN_SPI_SCLK GPIO_NUM_18
#define BSP_PIN_SPI_CS   GPIO_NUM_5 /* manual (software) chip select */
#define BSP_PIN_INT1     GPIO_NUM_2 /* sensor INT1 line - hardware trigger, CHIRP_SENSOR_TRIG_PIN */
#define BSP_PIN_INT2     GPIO_NUM_4 /* sensor INT2 line - data-ready interrupt, CHIRP_SENSOR_INT_PIN */

#define BSP_SPI_HOST          SPI2_HOST
#define BSP_SPI_CLOCK_HZ      (1 * 1000 * 1000)
#define BSP_SPI_SCRATCH_BYTES (MAX_PROG_XFER_SIZE + 4)

/*!< Set once, before ch_group_start(), by chbsp_esp32_init(). Used by the GPIO ISR and by the
 * chbsp_group_* functions to reach the sensor's ch_dev_t (this board has a single sensor). */
extern ch_group_t *bsp_grp_ptr;

/*!< SPI device handle for the sensor, added to BSP_SPI_HOST in chbsp_esp32_init(). */
extern spi_device_handle_t bsp_spi_handle;

/*!< DMA-capable scratch buffer used by chbsp_spi_write()/chbsp_spi_read() so that transfers never
 * depend on the caller's buffer being DMA-capable. */
extern uint8_t *bsp_spi_scratch;

/*!< Signals chbsp_event_wait() from chbsp_event_notify(). Set from bsp_int_task (task context),
 * not from an ISR - see the interrupt note below. */
extern EventGroupHandle_t bsp_event_group;

/*!< Task that runs SonicLib's ch_interrupt() at task level. The GPIO ISR only notifies it; see
 * bsp_int_task() in esp32_bsp.c for why (chdrv_int_callback_deferred() does blocking SPI). Created
 * by chbsp_esp32_init(). */
extern TaskHandle_t bsp_int_task_handle;

/*!< GPIO ISR handler for BSP_PIN_INT2 (data-ready). Only gives a notification to bsp_int_task_handle;
 * defined in esp32_bsp.c, attached to the pin by chbsp_esp32_init(). */
void bsp_int2_isr_handler(void *arg);

/*!< The bsp_int_task body, defined in esp32_bsp.c, started by chbsp_esp32_init(). */
void bsp_int_task(void *arg);

#ifdef __cplusplus
}
#endif

#endif /* ESP32_BSP_INTERNAL_H_ */
