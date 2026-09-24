/* Must precede every include (even transitive ones) - see Kconfig.projbuild's FTSP_MUTE_LOGS. */
#ifdef FTSP_MUTE_LOGS
#define LOG_LOCAL_LEVEL ESP_LOG_NONE
#endif

#include "gpio_strobe.h"

#include <stdbool.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "ftsp-strobe";

typedef struct {
    ftsp_sync_ctx_t    *sync_ctx;
    esp_timer_handle_t  rise_timer;
    esp_timer_handle_t  fall_timer;
} gpio_strobe_state_t;

static gpio_strobe_state_t g_state;

static void schedule_next_rise(gpio_strobe_state_t *state) {
    ftsp_regression_t reg;
    bool               have_reg = ftsp_sync_get_regression(state->sync_ctx, &reg);
    bool               use_root_clock_directly =
        (ftsp_sync_get_role(state->sync_ctx) == FTSP_ROLE_ROOT) || !have_reg || !ftsp_regression_is_valid(&reg);

    int64_t local_now = esp_timer_get_time();
    int64_t root_now  = use_root_clock_directly ? local_now : ftsp_regression_root_from_local(&reg, local_now);

    /* Prefer the root's own explicit choice of the next rendezvous instant (see ftsp_sync.h's
     * rendezvous note) over deriving one ourselves - the root is the sole authority on which
     * FTSP_EVENT_PERIOD_US boundary is "next" on its own clock. Fall back to deriving it only
     * when no still-future announcement is available (no SYNC yet, or one hasn't refreshed since
     * the last strobe): still correct, just loses the "root decides" property for this cycle. */
    int64_t next_boundary_root;
    if (!ftsp_sync_get_next_strobe_root_us(state->sync_ctx, &next_boundary_root) || next_boundary_root <= root_now) {
        next_boundary_root = ((root_now / GPIO_STROBE_PERIOD_US) + 1) * GPIO_STROBE_PERIOD_US;
    }

    int64_t next_boundary_local =
        use_root_clock_directly ? next_boundary_root : ftsp_regression_local_from_root(&reg, next_boundary_root);

    int64_t delay_us = next_boundary_local - esp_timer_get_time();
    if (delay_us < 0) {
        delay_us = 0;
    }

    esp_timer_start_once(state->rise_timer, (uint64_t)delay_us);
}

/* Runs in the esp_timer service task - kept to a GPIO write plus arming the next timer, no
 * logging in the steady-state path. */
static void rise_cb(void *arg) {
    gpio_strobe_state_t *state = (gpio_strobe_state_t *)arg;
    gpio_set_level(GPIO_STROBE_PIN, 1);
    esp_timer_start_once(state->fall_timer, GPIO_STROBE_HIGH_US);
}

static void fall_cb(void *arg) {
    gpio_strobe_state_t *state = (gpio_strobe_state_t *)arg;
    gpio_set_level(GPIO_STROBE_PIN, 0);
    /* Re-derive the next deadline from the freshest regression fit every cycle, rather than
     * scheduling a whole run of pulses up front, so skew corrections keep applying. */
    schedule_next_rise(state);
}

esp_err_t gpio_strobe_init(void) {
    gpio_config_t io_conf = {
        .pin_bit_mask = 1ULL << GPIO_STROBE_PIN,
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&io_conf);
    if (err != ESP_OK) {
        return err;
    }
    err = gpio_set_level(GPIO_STROBE_PIN, 0);
    if (err != ESP_OK) {
        return err;
    }

    const esp_timer_create_args_t rise_args = {
        .callback = rise_cb,
        .arg      = &g_state,
        .name     = "strobe_rise",
    };
    err = esp_timer_create(&rise_args, &g_state.rise_timer);
    if (err != ESP_OK) {
        return err;
    }

    const esp_timer_create_args_t fall_args = {
        .callback = fall_cb,
        .arg      = &g_state,
        .name     = "strobe_fall",
    };
    return esp_timer_create(&fall_args, &g_state.fall_timer);
}

void gpio_strobe_run(ftsp_sync_ctx_t *ctx) {
    g_state.sync_ctx = ctx;

    /* Wait for a usable clock: root time is authoritative for the root itself, or a follower
     * needs at least one fitted regression before its estimate of root time means anything. */
    for (;;) {
        ftsp_role_t       role = ftsp_sync_get_role(ctx);
        ftsp_regression_t reg;
        if (role == FTSP_ROLE_ROOT) {
            break;
        }
        if (role == FTSP_ROLE_FOLLOWER && ftsp_sync_get_regression(ctx, &reg) && ftsp_regression_is_valid(&reg)) {
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }

    ESP_LOGI(TAG, "clock ready - starting strobe on GPIO%d", GPIO_STROBE_PIN);
    schedule_next_rise(&g_state);

    for (;;) {
        vTaskDelay(portMAX_DELAY);
    }
}
