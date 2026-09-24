/*! \file ftsp_main.c
 *
 * \brief Entry point for the ESP-NOW FTSP capability test. Identical firmware runs on both
 * boards: ftsp_sync elects a root by lowest MAC and fits a clock regression against it, then
 * gpio_strobe schedules a synchronized GPIO25 pulse train against that shared clock. See
 * ftsp_sync.h for the protocol/election details.
 */

/* Must precede every include (even transitive ones) - see Kconfig.projbuild's FTSP_MUTE_LOGS. */
#ifdef FTSP_MUTE_LOGS
#define LOG_LOCAL_LEVEL ESP_LOG_NONE
#endif

#include "esp_log.h"

#include "espnow_link.h"
#include "ftsp_sync.h"
#include "gpio_strobe.h"

static const char *TAG = "ftsp-main";

void app_main(void) {
    ESP_LOGI(TAG, "ESP-NOW FTSP capability test");

    ftsp_sync_ctx_t *ctx;
    ESP_ERROR_CHECK(ftsp_sync_init(&ctx));
    ESP_ERROR_CHECK(espnow_link_init(ftsp_sync_get_rx_queue(ctx)));
    ESP_ERROR_CHECK(ftsp_sync_start(ctx));
    ESP_ERROR_CHECK(gpio_strobe_init());

    gpio_strobe_run(ctx); /* never returns */
}
