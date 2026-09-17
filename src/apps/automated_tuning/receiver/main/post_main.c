/*! \file post_main.c
 *
 * \brief Hardware bring-up entry point. Runs the ICU-20201 power-on self-test (the icu_post
 * component) and reports the result to the USB serial console, retrying on failure so the
 * FFC / wiring can be re-seated and watched for recovery.
 *
 * This is the automated-tuning receiver: it owns the shared-INT1 measurement trigger loop and
 * exposes a binary RPC over USB serial (see at_protocol.h) that a host script uses to retune the
 * sensor's configuration live and relay it to the transmitter over ESP-NOW.
 */

#include <inttypes.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include <invn/soniclib/soniclib.h>
#include <invn/soniclib/sensor_fw/icu_gpt/icu_gpt.h>

#include "chbsp_esp32_init.h"
#include "icu_post.h"
#include "trigger_loop.h"

static const char *TAG = "at-rx";

static ch_group_t grp;
static ch_dev_t   dev;

void app_main(void) {
	ESP_LOGI(TAG, "ICU-20201 hardware bring-up: power-on self-test (automated-tuning receiver)");

	/* SonicLib data-structure init - no sensor I/O happens here. */
	ch_group_init(&grp, CHIRP_MAX_NUM_SENSORS, CHIRP_NUM_BUSES, CHIRP_RTC_CAL_PULSE_MS);
	uint8_t rc = ch_init(&dev, &grp, 0, icu_gpt_init);
	if (rc != 0) {
		/* A non-zero rc here is a firmware-image / build problem, not a wiring problem. */
		ESP_LOGE(TAG, "ch_init() failed rc=%u - halting", rc);
		return;
	}

	/* Board hardware: GPIO directions, SPI bus + device (mode 3), INT2 data-ready ISR, and the
	 * FreeRTOS event group SonicLib waits on during ch_group_start(). Must run before any
	 * SonicLib call that reaches the sensor. */
	esp_err_t err = chbsp_esp32_init(&grp);
	if (err != ESP_OK) {
		ESP_LOGE(TAG, "chbsp_esp32_init() failed: %s - halting", esp_err_to_name(err));
		return;
	}
	ESP_LOGI(TAG, "BSP init OK");

	post_config_t cfg;
	post_config_default(&cfg);

	post_result_t res;
	bool pass = false;
	for (int attempt = 1; !pass; attempt++) {
		ESP_LOGI(TAG, "---- attempt %d ----", attempt);
		pass = post_run(&grp, &dev, &cfg, &res);
		post_report(&res);
		if (!pass) {
			ESP_LOGW(TAG, "POST failed - re-check FFC seating, wiring, and the 1.8V rail; retrying in 5 s");
			vTaskDelay(pdMS_TO_TICKS(5000));
		}
	}

	ESP_LOGI(TAG, "POST passed - ICU-20201 is alive (part %u, %" PRIu32 " Hz)", res.part_number,
	         res.op_frequency_hz);

	/* Hand off to the receiver - installs the serial RPC link, ESP-NOW relay, and the trigger
	 * loop. Does not return. */
	at_trigger_run(&grp, &dev);
}
