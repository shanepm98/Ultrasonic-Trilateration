/*! \file chbsp_esp32_init.h
 *
 * \brief One-time hardware initialization for the ESP32 SonicLib BSP.
 *
 * Every chbsp_ function in esp32_bsp.c depends on the GPIO, SPI, interrupt, and FreeRTOS
 * event-group setup performed here. The application must call chbsp_esp32_init() after
 * ch_group_init() and ch_init() (so the ch_group_t/ch_dev_t are valid) and before
 * ch_group_start() (so the hardware is ready when the driver starts probing the sensor).
 */

#ifndef CHBSP_ESP32_INIT_H_
#define CHBSP_ESP32_INIT_H_

#include <invn/soniclib/soniclib.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/*!
 * \brief Initialize GPIO, SPI, interrupt, and event-group resources for the sensor group.
 *
 * \param grp_ptr pointer to the ch_group_t descriptor, already passed to ch_group_init()
 *
 * \return ESP_OK on success, an ESP-IDF error code otherwise
 *
 * Must be called exactly once, after ch_group_init()/ch_init() and before ch_group_start().
 */
esp_err_t chbsp_esp32_init(ch_group_t *grp_ptr);

#ifdef __cplusplus
}
#endif

#endif /* CHBSP_ESP32_INIT_H_ */
