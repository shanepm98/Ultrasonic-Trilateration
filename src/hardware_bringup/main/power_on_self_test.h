/*! \file power_on_self_test.h
 *
 * \brief Power-on self-test (POST) for a single ICU-20201 ultrasonic sensor reached over SPI
 *        from the ESP32 via the SonicLib board support package (src/soniclib_esp32_bsp).
 *
 * The point of this module is a fast, reusable "is the sensor alive and talking?" check that
 * later firmware iterations can call at boot before doing anything else. It is deliberately
 * SonicLib-level only (no direct ESP32 / esp-idf calls) so it stays valid if the BSP or wiring
 * changes - all hardware access goes through the chbsp_* callbacks the BSP already implements.
 *
 * Stages, in order (see post_stage_t):
 *   1. SPI link       - chdrv_prog_ping(): reset the sensor through its SYS_CTRL register and
 *                       read back the CPU ID debug register over SPI, verifying it matches
 *                       SHASTA_CPU_ID_HI_VALUE. A pass proves wiring, chip-select, SPI mode,
 *                       bit order and framing are all correct end to end.
 *   2. Program + start - ch_group_start(): full discovery, firmware download, frequency lock
 *                       and RTC calibration; then ch_sensor_is_connected().
 *   3. Sensor identity - read back part number / serial / firmware version / operating
 *                       frequency / RTC cal result and sanity-check them.
 *
 * Usage (see post_main.c):
 *
 *     ch_group_init(&grp, CHIRP_MAX_NUM_SENSORS, CHIRP_NUM_BUSES, CHIRP_RTC_CAL_PULSE_MS);
 *     ch_init(&dev, &grp, 0, icu_gpt_init);
 *     chbsp_esp32_init(&grp);              // BSP hardware setup - caller's job, not post_run()'s
 *
 *     post_config_t cfg;
 *     post_config_default(&cfg);
 *     post_result_t res;
 *     bool ok = post_run(&grp, &dev, &cfg, &res);
 *     post_report(&res);
 */

#ifndef HW_BRINGUP_POWER_ON_SELF_TEST_H_
#define HW_BRINGUP_POWER_ON_SELF_TEST_H_

#include <stdbool.h>
#include <stdint.h>

#include <invn/soniclib/soniclib.h>

#ifdef __cplusplus
extern "C" {
#endif

/*! Individual POST checks, in execution order. */
typedef enum {
	POST_STAGE_SPI_LINK = 0, /*!< chdrv_prog_ping() - low-level SPI register read + CPU ID match */
	POST_STAGE_GROUP_START,   /*!< ch_group_start() + ch_sensor_is_connected() */
	POST_STAGE_SENSOR_ID,     /*!< identity / parameter read-back and sanity check */
	POST_STAGE_COUNT
} post_stage_t;

/*! Per-stage outcome. */
typedef struct {
	bool run;          /*!< stage was attempted (later stages are skipped after a hard failure) */
	bool pass;         /*!< stage passed */
	int  code;          /*!< stage-specific status: esp_err_t / SonicLib rc / 0 on pass */
	char detail[72];   /*!< short human-readable explanation */
} post_stage_result_t;

/*! Full POST result. Safe to pass NULL to post_run() if only the return value + log matter. */
typedef struct {
	bool all_pass;    /*!< every attempted stage passed */
	bool spi_link_ok; /*!< headline result: the ESP32 can talk to the module over SPI */

	post_stage_result_t stage[POST_STAGE_COUNT];

	/* Filled in once POST_STAGE_SENSOR_ID runs: */
	uint16_t part_number;     /*!< expect 20201 (ICU20201_PART_NUMBER) */
	char     sensor_id[32];   /*!< lot + serial from OTP, or "NOTPROG" if sensor OTP is blank */
	char     fw_version[40];  /*!< SonicLib sensor firmware version string */
	uint32_t op_frequency_hz; /*!< acoustic operating frequency (85 kHz nominal, 70-95 kHz, per DS-000478) */
	uint16_t rtc_cal_result;  /*!< RTC calibration count from ch_group_start() */
} post_result_t;

/*! POST options. Initialise with post_config_default(). */
typedef struct {
	bool     run_group_start;       /*!< run stages 2-3; false = SPI-link-only smoke test */
	bool     stop_on_fail;          /*!< abort remaining stages after the first hard failure */
	uint16_t expected_part_number;  /*!< fail stage 3 if part number differs; 0 disables the check */
} post_config_t;

/*!
 * \brief Populate \a cfg with defaults: full test, stop-on-fail, expect ICU-20201.
 */
void post_config_default(post_config_t *cfg);

/*!
 * \brief Run the power-on self-test against one already-initialised sensor.
 *
 * \param grp  sensor group, already passed to ch_group_init() and chbsp_esp32_init()
 * \param dev  sensor device, already passed to ch_init(&dev, grp, 0, icu_gpt_init)
 * \param cfg  options, or NULL for post_config_default()
 * \param out  result destination, or NULL
 *
 * \return true iff every attempted stage passed
 *
 * Does not call chbsp_esp32_init() - the caller must have done that (and ch_group_init /
 * ch_init) first, matching the BSP usage contract in docs/board_support_package.md.
 */
bool post_run(ch_group_t *grp, ch_dev_t *dev, const post_config_t *cfg, post_result_t *out);

/*!
 * \brief Print a result block to the console (ESP_LOG, tag "POST").
 *
 * Callable any time after post_run(); handy for re-dumping the last result from application code.
 */
void post_report(const post_result_t *res);

#ifdef __cplusplus
}
#endif

#endif /* HW_BRINGUP_POWER_ON_SELF_TEST_H_ */
