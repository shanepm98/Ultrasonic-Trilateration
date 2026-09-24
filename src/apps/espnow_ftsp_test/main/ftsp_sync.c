/* Must precede every include (even transitive ones) - see Kconfig.projbuild's FTSP_MUTE_LOGS. */
#ifdef FTSP_MUTE_LOGS
#define LOG_LOCAL_LEVEL ESP_LOG_NONE
#endif

#include "ftsp_sync.h"

#include <inttypes.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "esp_mac.h"
#include "esp_timer.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "espnow_link.h"

static const char *TAG = "ftsp-sync";

struct ftsp_sync_ctx {
    QueueHandle_t     rx_queue;
    SemaphoreHandle_t mutex;
    uint8_t           own_mac[6];
    uint8_t           root_mac[6];         /* mutex-protected */
    ftsp_role_t       role;                /* mutex-protected */
    ftsp_regression_t regression;          /* mutex-protected */
    int64_t           next_strobe_root_us; /* mutex-protected; meaningless unless has_next_strobe */
    bool              has_next_strobe;     /* mutex-protected */
};

/* ---- regression math ---- */

void ftsp_regression_init(ftsp_regression_t *reg) {
    memset(reg, 0, sizeof(*reg));
}

static void ftsp_regression_refit(ftsp_regression_t *reg) {
    if (reg->count < 2) {
        reg->skew         = 0.0;
        reg->intercept_us = (reg->count == 1) ? (double)reg->points[0].offset_us : 0.0;
        reg->valid         = false;
        return;
    }

    double mean_x = 0.0, mean_y = 0.0;
    for (uint8_t i = 0; i < reg->count; i++) {
        mean_x += (double)(reg->points[i].local_us - reg->x0);
        mean_y += (double)reg->points[i].offset_us;
    }
    mean_x /= reg->count;
    mean_y /= reg->count;

    double num = 0.0, denom = 0.0;
    for (uint8_t i = 0; i < reg->count; i++) {
        double x = (double)(reg->points[i].local_us - reg->x0) - mean_x;
        double y = (double)reg->points[i].offset_us - mean_y;
        num += x * y;
        denom += x * x;
    }

    if (fabs(denom) < 1e-9) {
        reg->skew         = 0.0;
        reg->intercept_us = mean_y;
    } else {
        reg->skew         = num / denom;
        reg->intercept_us = mean_y - reg->skew * mean_x;
    }
    /* Below FTSP_MIN_SAMPLES_FOR_VALID, a fit exists (usable internally to bootstrap outlier
     * rejection below) but isn't trusted by callers yet - see the header comment on
     * FTSP_MIN_SAMPLES_FOR_VALID for why a 2-point fit is too outlier-sensitive to use. */
    reg->valid = (reg->count >= FTSP_MIN_SAMPLES_FOR_VALID);
}

bool ftsp_regression_add_sample(ftsp_regression_t *reg, int64_t local_us, int64_t root_us) {
    if (reg->valid) {
        int64_t predicted_root = ftsp_regression_root_from_local(reg, local_us);
        int64_t residual       = root_us - predicted_root;
        if (llabs(residual) > FTSP_OUTLIER_THRESHOLD_US) {
            return false; /* leave the table/fit untouched - caller logs the rejection */
        }
    }

    if (reg->count == 0) {
        reg->x0 = local_us;
    }

    reg->points[reg->next_idx].local_us  = local_us;
    reg->points[reg->next_idx].offset_us = root_us - local_us;
    reg->next_idx                        = (uint8_t)((reg->next_idx + 1) % FTSP_TABLE_SIZE);
    if (reg->count < FTSP_TABLE_SIZE) {
        reg->count++;
    }

    ftsp_regression_refit(reg);
    return true;
}

bool ftsp_regression_is_valid(const ftsp_regression_t *reg) {
    return reg->valid;
}

int64_t ftsp_regression_root_from_local(const ftsp_regression_t *reg, int64_t local_us) {
    if (!reg->valid) {
        return local_us;
    }
    double x    = (double)(local_us - reg->x0);
    double root = (double)local_us + reg->intercept_us + reg->skew * x;
    return (int64_t)root;
}

int64_t ftsp_regression_local_from_root(const ftsp_regression_t *reg, int64_t root_us) {
    if (!reg->valid) {
        return root_us;
    }
    /* root = local + intercept + skew*(local - x0)  =>  local = (root - intercept + skew*x0) / (1 + skew) */
    double denom = 1.0 + reg->skew;
    if (fabs(denom) < 1e-9) {
        return root_us; /* degenerate skew - avoid dividing by near-zero */
    }
    double local = ((double)root_us - reg->intercept_us + reg->skew * (double)reg->x0) / denom;
    return (int64_t)local;
}

/* ---- ctx accessors (all mutex-protected; safe from any task) ---- */

static void ctx_get_root_mac(ftsp_sync_ctx_t *ctx, uint8_t out[6]) {
    xSemaphoreTake(ctx->mutex, portMAX_DELAY);
    memcpy(out, ctx->root_mac, 6);
    xSemaphoreGive(ctx->mutex);
}

static void ctx_finish_discovery(ftsp_sync_ctx_t *ctx, const uint8_t root_mac[6]) {
    xSemaphoreTake(ctx->mutex, portMAX_DELAY);
    memcpy(ctx->root_mac, root_mac, 6);
    ctx->role = (memcmp(root_mac, ctx->own_mac, 6) == 0) ? FTSP_ROLE_ROOT : FTSP_ROLE_FOLLOWER;
    ftsp_regression_init(&ctx->regression);
    ctx->has_next_strobe = false; /* a prior election's rendezvous target means nothing now */
    xSemaphoreGive(ctx->mutex);
}

static void ctx_adopt_root(ftsp_sync_ctx_t *ctx, const uint8_t root_mac[6]) {
    xSemaphoreTake(ctx->mutex, portMAX_DELAY);
    memcpy(ctx->root_mac, root_mac, 6);
    ctx->role = FTSP_ROLE_FOLLOWER;
    ftsp_regression_init(&ctx->regression);
    ctx->has_next_strobe = false;
    xSemaphoreGive(ctx->mutex);
}

static void ctx_reset_to_discovering(ftsp_sync_ctx_t *ctx) {
    xSemaphoreTake(ctx->mutex, portMAX_DELAY);
    memcpy(ctx->root_mac, ctx->own_mac, 6);
    ctx->role = FTSP_ROLE_DISCOVERING;
    ftsp_regression_init(&ctx->regression);
    ctx->has_next_strobe = false;
    xSemaphoreGive(ctx->mutex);
}

static bool ctx_add_sample(ftsp_sync_ctx_t *ctx, int64_t local_us, int64_t root_us) {
    xSemaphoreTake(ctx->mutex, portMAX_DELAY);
    bool accepted = ftsp_regression_add_sample(&ctx->regression, local_us, root_us);
    xSemaphoreGive(ctx->mutex);
    return accepted;
}

static void ctx_set_next_strobe(ftsp_sync_ctx_t *ctx, int64_t next_strobe_root_us) {
    xSemaphoreTake(ctx->mutex, portMAX_DELAY);
    ctx->next_strobe_root_us = next_strobe_root_us;
    ctx->has_next_strobe     = true;
    xSemaphoreGive(ctx->mutex);
}

ftsp_role_t ftsp_sync_get_role(const ftsp_sync_ctx_t *ctx) {
    ftsp_sync_ctx_t *mctx = (ftsp_sync_ctx_t *)ctx;
    xSemaphoreTake(mctx->mutex, portMAX_DELAY);
    ftsp_role_t role = mctx->role;
    xSemaphoreGive(mctx->mutex);
    return role;
}

bool ftsp_sync_get_regression(const ftsp_sync_ctx_t *ctx, ftsp_regression_t *out) {
    if (ctx == NULL) {
        return false;
    }
    ftsp_sync_ctx_t *mctx = (ftsp_sync_ctx_t *)ctx;
    xSemaphoreTake(mctx->mutex, portMAX_DELAY);
    *out = mctx->regression;
    xSemaphoreGive(mctx->mutex);
    return true;
}

bool ftsp_sync_get_next_strobe_root_us(const ftsp_sync_ctx_t *ctx, int64_t *out) {
    if (ctx == NULL) {
        return false;
    }
    ftsp_sync_ctx_t *mctx = (ftsp_sync_ctx_t *)ctx;
    xSemaphoreTake(mctx->mutex, portMAX_DELAY);
    bool has = mctx->has_next_strobe;
    if (has) {
        *out = mctx->next_strobe_root_us;
    }
    xSemaphoreGive(mctx->mutex);
    return has;
}

/* ---- wire helpers ---- */

static void send_hello(const uint8_t claimed_root_mac[6]) {
    ftsp_msg_hello_t msg = {
        .msg_type      = FTSP_MSG_HELLO,
        .proto_version = FTSP_PROTOCOL_VERSION,
    };
    memcpy(msg.claimed_root_mac, claimed_root_mac, 6);
    esp_err_t err = espnow_link_send(&msg, sizeof(msg));
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "HELLO send failed: %s", esp_err_to_name(err));
    }
}

/*! \brief Round local_us up to the next multiple of FTSP_EVENT_PERIOD_US. On the root's own
 * clock this *is* root time, so this is also the rule followers fall back to (see
 * gpio_strobe.c) when no still-future explicit announcement is available yet. */
static int64_t next_event_boundary(int64_t local_us) {
    return ((local_us / FTSP_EVENT_PERIOD_US) + 1) * FTSP_EVENT_PERIOD_US;
}

static void send_sync(ftsp_sync_ctx_t *ctx, uint32_t seq) {
    int64_t now          = esp_timer_get_time(); /* read as close to the send as possible */
    int64_t next_strobe   = next_event_boundary(now);
    ctx_set_next_strobe(ctx, next_strobe); /* root is authoritative for its own announcement too */

    ftsp_msg_sync_t msg = {
        .msg_type            = FTSP_MSG_SYNC,
        .proto_version       = FTSP_PROTOCOL_VERSION,
        .seq                 = seq,
        .root_timestamp_us   = now,
        .next_strobe_root_us = next_strobe,
    };
    memcpy(msg.root_mac, ctx->own_mac, 6);
    esp_err_t err = espnow_link_send(&msg, sizeof(msg));
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "SYNC send failed: %s", esp_err_to_name(err));
    }
}

/*! \brief Extract the MAC this packet claims as root (HELLO: claimed_root_mac, SYNC: root_mac).
 * Returns false if the packet isn't a recognized, correct-version FTSP message. */
static bool parse_claimed_root(const espnow_link_rx_event_t *evt, uint8_t mac_out[6]) {
    if (evt->len == sizeof(ftsp_msg_hello_t)) {
        const ftsp_msg_hello_t *m = (const ftsp_msg_hello_t *)evt->data;
        if (m->msg_type == FTSP_MSG_HELLO && m->proto_version == FTSP_PROTOCOL_VERSION) {
            memcpy(mac_out, m->claimed_root_mac, 6);
            return true;
        }
    } else if (evt->len == sizeof(ftsp_msg_sync_t)) {
        const ftsp_msg_sync_t *m = (const ftsp_msg_sync_t *)evt->data;
        if (m->msg_type == FTSP_MSG_SYNC && m->proto_version == FTSP_PROTOCOL_VERSION) {
            memcpy(mac_out, m->root_mac, 6);
            return true;
        }
    }
    return false;
}

/* ---- role loops - each returns as soon as ctx's role changes out from under it ---- */

static void run_discovery(ftsp_sync_ctx_t *ctx) {
    uint8_t best_root_mac[6];
    memcpy(best_root_mac, ctx->own_mac, 6);

    TickType_t start      = xTaskGetTickCount();
    TickType_t last_hello = start - pdMS_TO_TICKS(FTSP_HELLO_INTERVAL_MS);

    while ((xTaskGetTickCount() - start) < pdMS_TO_TICKS(FTSP_DISCOVERY_WINDOW_MS)) {
        if ((xTaskGetTickCount() - last_hello) >= pdMS_TO_TICKS(FTSP_HELLO_INTERVAL_MS)) {
            send_hello(best_root_mac);
            last_hello = xTaskGetTickCount();
        }

        espnow_link_rx_event_t evt;
        if (xQueueReceive(ctx->rx_queue, &evt, pdMS_TO_TICKS(20)) == pdTRUE) {
            uint8_t candidate[6];
            if (parse_claimed_root(&evt, candidate)) {
                /* Logged unconditionally (not just on a change of best_root_mac) - this is the
                 * only place that proves ESP-NOW reception is working at all. If the peer board
                 * never shows up here, nothing downstream (election, regression, strobe sync)
                 * can possibly work - go debug the radio link before anything else. */
                ESP_LOGI(TAG, "discovery: heard claim " MACSTR " from " MACSTR, MAC2STR(candidate), MAC2STR(evt.src_mac));
                if (memcmp(candidate, best_root_mac, 6) < 0) {
                    memcpy(best_root_mac, candidate, 6);
                }
            }
        }
    }

    ctx_finish_discovery(ctx, best_root_mac);
    ESP_LOGI(TAG, "discovery complete - role=%s, root=" MACSTR,
             memcmp(best_root_mac, ctx->own_mac, 6) == 0 ? "ROOT" : "FOLLOWER", MAC2STR(best_root_mac));
}

static void run_root(ftsp_sync_ctx_t *ctx) {
    uint32_t   seq       = 0;
    TickType_t last_sync = xTaskGetTickCount() - pdMS_TO_TICKS(FTSP_SYNC_INTERVAL_MS);

    while (ftsp_sync_get_role(ctx) == FTSP_ROLE_ROOT) {
        if ((xTaskGetTickCount() - last_sync) >= pdMS_TO_TICKS(FTSP_SYNC_INTERVAL_MS)) {
            send_sync(ctx, seq++);
            last_sync = xTaskGetTickCount();
        }

        espnow_link_rx_event_t evt;
        if (xQueueReceive(ctx->rx_queue, &evt, pdMS_TO_TICKS(20)) == pdTRUE) {
            uint8_t candidate[6];
            if (parse_claimed_root(&evt, candidate) && memcmp(candidate, ctx->own_mac, 6) < 0) {
                ESP_LOGI(TAG, "saw lower MAC " MACSTR " claiming root - self-demoting", MAC2STR(candidate));
                ctx_adopt_root(ctx, candidate);
                return;
            }
        }
    }
}

static void run_follower(ftsp_sync_ctx_t *ctx) {
    while (ftsp_sync_get_role(ctx) == FTSP_ROLE_FOLLOWER) {
        espnow_link_rx_event_t evt;
        if (xQueueReceive(ctx->rx_queue, &evt, pdMS_TO_TICKS(FTSP_ROOT_TIMEOUT_MS)) != pdTRUE) {
            ESP_LOGW(TAG, "root silent for %u ms - returning to discovery", (unsigned)FTSP_ROOT_TIMEOUT_MS);
            ctx_reset_to_discovering(ctx);
            return;
        }

        uint8_t root_mac[6];
        ctx_get_root_mac(ctx, root_mac);

        if (evt.len == sizeof(ftsp_msg_sync_t)) {
            const ftsp_msg_sync_t *m = (const ftsp_msg_sync_t *)evt.data;
            if (m->msg_type != FTSP_MSG_SYNC || m->proto_version != FTSP_PROTOCOL_VERSION) {
                continue;
            }
            if (memcmp(m->root_mac, root_mac, 6) < 0) {
                ESP_LOGI(TAG, "follower: adopting lower-MAC root " MACSTR " (was " MACSTR ")", MAC2STR(m->root_mac),
                         MAC2STR(root_mac));
                ctx_adopt_root(ctx, m->root_mac);
                ctx_get_root_mac(ctx, root_mac);
            }
            if (memcmp(m->root_mac, root_mac, 6) == 0) {
                bool accepted = ctx_add_sample(ctx, evt.recv_local_us, m->root_timestamp_us);
                /* Trust the root's own choice of the next rendezvous instant outright - don't
                 * independently re-derive it. See the rendezvous note at the top of ftsp_sync.h. */
                ctx_set_next_strobe(ctx, m->next_strobe_root_us);
                /* Verbose (fires every SYNC_INTERVAL) - useful during bring-up to confirm the
                 * regression is actually being fed; drop to ESP_LOGD once the link is trusted. */
                ESP_LOGI(TAG, "follower: sample #%" PRIu32 " local=%lld root=%lld offset=%lld next_strobe=%lld%s", m->seq,
                         (long long)evt.recv_local_us, (long long)m->root_timestamp_us,
                         (long long)(m->root_timestamp_us - evt.recv_local_us), (long long)m->next_strobe_root_us,
                         accepted ? "" : " [REJECTED outlier]");
            }
            continue;
        }

        uint8_t candidate[6];
        if (parse_claimed_root(&evt, candidate) && memcmp(candidate, root_mac, 6) < 0) {
            ESP_LOGI(TAG, "follower: adopting lower-MAC root " MACSTR " (was " MACSTR ") from HELLO", MAC2STR(candidate),
                     MAC2STR(root_mac));
            ctx_adopt_root(ctx, candidate);
        }
    }
}

static void ftsp_sync_task(void *arg) {
    ftsp_sync_ctx_t *ctx = (ftsp_sync_ctx_t *)arg;

    ESP_ERROR_CHECK(espnow_link_get_own_mac(ctx->own_mac));
    memcpy(ctx->root_mac, ctx->own_mac, 6);
    ESP_LOGI(TAG, "own MAC " MACSTR, MAC2STR(ctx->own_mac));

    for (;;) {
        switch (ftsp_sync_get_role(ctx)) {
        case FTSP_ROLE_DISCOVERING:
            run_discovery(ctx);
            break;
        case FTSP_ROLE_ROOT:
            run_root(ctx);
            break;
        case FTSP_ROLE_FOLLOWER:
            run_follower(ctx);
            break;
        }
    }
}

/* ---- public lifecycle ---- */

esp_err_t ftsp_sync_init(ftsp_sync_ctx_t **out_ctx) {
    ftsp_sync_ctx_t *ctx = calloc(1, sizeof(*ctx));
    if (ctx == NULL) {
        return ESP_ERR_NO_MEM;
    }

    ctx->rx_queue = xQueueCreate(16, sizeof(espnow_link_rx_event_t));
    if (ctx->rx_queue == NULL) {
        free(ctx);
        return ESP_ERR_NO_MEM;
    }
    ctx->mutex = xSemaphoreCreateMutex();
    if (ctx->mutex == NULL) {
        vQueueDelete(ctx->rx_queue);
        free(ctx);
        return ESP_ERR_NO_MEM;
    }

    ctx->role = FTSP_ROLE_DISCOVERING;
    ftsp_regression_init(&ctx->regression);

    *out_ctx = ctx;
    return ESP_OK;
}

esp_err_t ftsp_sync_start(ftsp_sync_ctx_t *ctx) {
    BaseType_t ok = xTaskCreate(ftsp_sync_task, "ftsp_sync", 4096, ctx, 5, NULL);
    return (ok == pdPASS) ? ESP_OK : ESP_FAIL;
}

QueueHandle_t ftsp_sync_get_rx_queue(ftsp_sync_ctx_t *ctx) {
    return ctx->rx_queue;
}
