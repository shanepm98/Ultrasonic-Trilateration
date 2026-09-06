/*! \file icu_post.c
 *
 * \brief Implementation of the ICU-20201 power-on self-test. See icu_post.h.
 */

#include "icu_post.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "esp_log.h"

#include <invn/soniclib/soniclib.h>
#include <invn/soniclib/details/ch_driver.h>       /* chdrv_prog_ping(), chdrv_dbg_reg_read() */
#include <invn/soniclib/details/ch_asic_shasta.h>  /* SYS_CTRL_*, SHASTA_DBG_REG_CPU_ID_HI */

/* The ICU-20201 is a Shasta-generation SPI part. SonicLib selects SPI vs I2C at compile time;
 * without INCLUDE_SHASTA_SUPPORT it falls back to the CHx01 / I2C path and this POST's SPI
 * checks would be meaningless. Catch a misconfigured build here. */
#if !defined(INCLUDE_SHASTA_SUPPORT) || defined(INCLUDE_WHITNEY_SUPPORT)
#error "Build SonicLib with INCLUDE_SHASTA_SUPPORT and without INCLUDE_WHITNEY_SUPPORT for the ICU-20201"
#endif

static const char *TAG = "POST";

/* ICU-20201 acoustic operating frequency f_op is 85 kHz nominal, 70-95 kHz over process/temp
 * (DS-000478: "nominally 85kHz PMUT"; Operating Frequency 70/85/95 kHz min/typ/max). Outside this
 * window is not an automatic failure (only a zero reading is), but it is worth a warning. */
#define POST_FREQ_PLAUSIBLE_LO_HZ 65000u
#define POST_FREQ_PLAUSIBLE_HI_HZ 100000u

void post_config_default(post_config_t *cfg) {
	if (cfg == NULL) {
		return;
	}
	cfg->run_group_start      = true;
	cfg->stop_on_fail         = true;
	cfg->expected_part_number = ICU20201_PART_NUMBER;
}

static void stage_set(post_stage_result_t *s, bool pass, int code, const char *detail) {
	s->run  = true;
	s->pass = pass;
	s->code = code;
	strncpy(s->detail, (detail != NULL) ? detail : "", sizeof(s->detail) - 1);
	s->detail[sizeof(s->detail) - 1] = '\0';
}

/* ---- Stage 1: SPI link -------------------------------------------------------------------- */
/* chdrv_prog_ping() exercises CS + SCLK + MOSI + MISO through the BSP's chbsp_spi_* callbacks:
 * it writes the sensor's SYS_CTRL register to hold and release reset, then reads the CPU_ID_HI
 * debug register and checks it equals SHASTA_CPU_ID_HI_VALUE. Returns 1 when the sensor answers
 * with the expected ID. This is the core "can the ESP32 talk to the module over SPI" check. */

/* Raw CPU_ID_HI read (mirrors the core of chdrv_prog_ping) so the log shows exactly what came
 * back over MISO - that value pins down the failure mode when the ping fails. */
static uint16_t read_cpu_id_hi(ch_dev_t *dev, uint8_t *err_out) {
	uint8_t  err = 0;
	uint16_t id  = 0;
	err |= chdrv_sys_ctrl_write(dev, SYS_CTRL_DEBUG);                     /* assert reset, debug mode */
	err |= chdrv_sys_ctrl_write(dev, SYS_CTRL_DEBUG | SYS_CTRL_RESET_N);  /* release reset */
	err |= chdrv_dbg_reg_read(dev, SHASTA_DBG_REG_CPU_ID_HI, &id);
	if (err_out != NULL) {
		*err_out = err;
	}
	return id;
}

static bool stage_spi_link(ch_dev_t *dev, post_stage_result_t *s) {
	uint8_t  rd_err = 0;
	uint16_t cpu_id = read_cpu_id_hi(dev, &rd_err);
	ESP_LOGI(TAG, "        CPU_ID_HI raw read = 0x%04X (want 0x%04X)%s", cpu_id, SHASTA_CPU_ID_HI_VALUE,
	         rd_err ? "  [BSP SPI transfer error]" : "");

	if (chdrv_prog_ping(dev)) {
		stage_set(s, true, 0, "prog_ping OK - sensor returned the expected CPU ID");
		ESP_LOGI(TAG, "[1/3] SPI link ......... PASS  (CPU ID 0x%04X matched)", SHASTA_CPU_ID_HI_VALUE);
		return true;
	}

	const char *hint;
	if (rd_err) {
		hint = "BSP SPI transfer returned error - SPI bus / chbsp_spi_* problem";
	} else if (cpu_id == 0x0000) {
		hint = "MISO reads all-0 - sensor unpowered (1.8V VDD?), no SCLK, or MISO not wired";
	} else if (cpu_id == 0xFFFF) {
		hint = "MISO reads all-1 - MISO floating / not driven; check MISO, CS (GPIO5), FFC seat";
	} else {
		hint = "wrong CPU ID - SPI mode (want mode 3), bit order, clock too fast, or level shifter";
	}
	stage_set(s, false, 1, hint);
	ESP_LOGE(TAG, "[1/3] SPI link ......... FAIL  (%s)", hint);
	return false;
}

/* ---- Stage 2: program + start ----------------------------------------------------------- */
static bool stage_group_start(ch_group_t *grp, ch_dev_t *dev, post_stage_result_t *s) {
	uint8_t rc = ch_group_start(grp);
	if (rc != 0) {
		char d[72];
		snprintf(d, sizeof(d), "ch_group_start() returned %u", rc);
		stage_set(s, false, (int)rc, d);
		ESP_LOGE(TAG, "[2/3] Program + start .. FAIL  (ch_group_start rc=%u)", rc);
		return false;
	}
	if (!ch_sensor_is_connected(dev)) {
		stage_set(s, false, 2, "ch_group_start() ok but sensor not marked connected");
		ESP_LOGE(TAG, "[2/3] Program + start .. FAIL  (sensor not connected after start)");
		return false;
	}
	stage_set(s, true, 0, "firmware loaded, frequency locked, RTC calibrated");
	ESP_LOGI(TAG, "[2/3] Program + start .. PASS");
	return true;
}

/* ---- Stage 3: identity + parameter sanity --------------------------------------------- */
static bool stage_sensor_id(ch_dev_t *dev, const post_config_t *cfg, post_result_t *out,
                            post_stage_result_t *s) {
	const char *id  = ch_get_sensor_id(dev);
	const char *fw  = ch_get_fw_version_string(dev);

	out->part_number    = ch_get_part_number(dev);
	out->op_frequency_hz = ch_get_frequency(dev);
	out->rtc_cal_result = ch_get_rtc_cal_result(dev);
	strncpy(out->sensor_id, (id != NULL) ? id : "", sizeof(out->sensor_id) - 1);
	out->sensor_id[sizeof(out->sensor_id) - 1] = '\0';
	strncpy(out->fw_version, (fw != NULL) ? fw : "", sizeof(out->fw_version) - 1);
	out->fw_version[sizeof(out->fw_version) - 1] = '\0';

	ESP_LOGI(TAG, "        part number ..... %u", out->part_number);
	ESP_LOGI(TAG, "        sensor id ....... %s", out->sensor_id);
	ESP_LOGI(TAG, "        fw version ...... %s", out->fw_version);
	ESP_LOGI(TAG, "        op frequency .... %" PRIu32 " Hz", out->op_frequency_hz);
	ESP_LOGI(TAG, "        rtc cal result .. %u", out->rtc_cal_result);

	bool pass = true;
	char d[72] = "identity read back over SPI";

	if (cfg->expected_part_number != 0 && out->part_number != cfg->expected_part_number) {
		pass = false;
		snprintf(d, sizeof(d), "part %u != expected %u", out->part_number, cfg->expected_part_number);
	} else if (out->op_frequency_hz == 0) {
		pass = false;
		strcpy(d, "operating frequency read back as 0");
	} else if (out->rtc_cal_result == 0) {
		pass = false;
		strcpy(d, "RTC calibration result is 0");
	}

	if (pass && (out->op_frequency_hz < POST_FREQ_PLAUSIBLE_LO_HZ ||
	             out->op_frequency_hz > POST_FREQ_PLAUSIBLE_HI_HZ)) {
		ESP_LOGW(TAG, "        (op frequency outside the 70-95 kHz band expected for ICU-20201)");
	}

	stage_set(s, pass, pass ? 0 : 3, d);
	if (pass) {
		ESP_LOGI(TAG, "[3/3] Sensor identity .. PASS");
	} else {
		ESP_LOGE(TAG, "[3/3] Sensor identity .. FAIL  (%s)", d);
	}
	return pass;
}

/* ---- Orchestration ------------------------------------------------------------------------ */
bool post_run(ch_group_t *grp, ch_dev_t *dev, const post_config_t *cfg, post_result_t *out) {
	post_config_t local_cfg;
	post_result_t local_res;

	if (cfg == NULL) {
		post_config_default(&local_cfg);
		cfg = &local_cfg;
	}
	if (out == NULL) {
		out = &local_res;
	}
	memset(out, 0, sizeof(*out));

	ESP_LOGI(TAG, "==== ICU-20201 power-on self-test ====");

	bool ok = true;
	bool r;

	r = stage_spi_link(dev, &out->stage[POST_STAGE_SPI_LINK]);
	out->spi_link_ok = r;
	ok = ok && r;
	if (!r && cfg->stop_on_fail) {
		goto done;
	}

	if (cfg->run_group_start) {
		r  = stage_group_start(grp, dev, &out->stage[POST_STAGE_GROUP_START]);
		ok = ok && r;
		if (!r && cfg->stop_on_fail) {
			goto done;
		}

		if (r) {
			r  = stage_sensor_id(dev, cfg, out, &out->stage[POST_STAGE_SENSOR_ID]);
			ok = ok && r;
		}
	} else {
		ESP_LOGW(TAG, "[2/3] Program + start .. SKIPPED (run_group_start = false)");
		ESP_LOGW(TAG, "[3/3] Sensor identity .. SKIPPED");
	}

done:
	out->all_pass = ok;
	ESP_LOGI(TAG, "==== POST %s ====", ok ? "PASS" : "FAIL");
	return ok;
}

void post_report(const post_result_t *res) {
	static const char *const names[POST_STAGE_COUNT] = {
			"SPI link (prog_ping)",
			"Program + start",
			"Sensor identity",
	};

	if (res == NULL) {
		return;
	}

	ESP_LOGI(TAG, "---- POST report ----");
	for (int i = 0; i < POST_STAGE_COUNT; i++) {
		const post_stage_result_t *s = &res->stage[i];
		if (!s->run) {
			ESP_LOGW(TAG, "  %-21s SKIP", names[i]);
		} else if (s->pass) {
			ESP_LOGI(TAG, "  %-21s PASS  %s", names[i], s->detail);
		} else {
			ESP_LOGE(TAG, "  %-21s FAIL  (code %d) %s", names[i], s->code, s->detail);
		}
	}
	ESP_LOGI(TAG, "  overall %s   spi_link_ok=%d", res->all_pass ? "PASS" : "FAIL", res->spi_link_ok);
	if (res->stage[POST_STAGE_SENSOR_ID].run) {
		ESP_LOGI(TAG, "  part=%u  id=%s  fw=%s  freq=%" PRIu32 "Hz  rtc=%u", res->part_number,
		         res->sensor_id, res->fw_version, res->op_frequency_hz, res->rtc_cal_result);
	}
}
