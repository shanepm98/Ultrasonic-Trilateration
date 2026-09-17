#include "at_transmitter.h"

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "esp_log.h"

#include <invn/soniclib/sensor_fw/icu_gpt/icu_gpt.h> /* icu_gpt_algo_*, InvnAlgoRangeFinderConfig */

#include "at_protocol.h"
#include "espnow_link.h"

static const char *TAG = "at-tx-apply";

static ch_dev_t                  *g_dev;
static InvnAlgoRangeFinderConfig  g_algo_cfg;
static QueueHandle_t              g_snapshot_queue;

static void at_thresholds_to_ch(const at_thresholds_t *in, ch_thresholds_t *out) {
    for (int i = 0; i < (int)AT_NUM_THRESHOLDS; i++) {
        out->threshold[i].start_sample = in->threshold[i].start_sample;
        out->threshold[i].level        = in->threshold[i].level;
    }
}

static void apply_snapshot(ch_dev_t *dev, const at_config_snapshot_t *snap) {
    /* Defensive bracket, same reasoning as the receiver's at_cfg_meas_reset(): SonicLib
     * documents no "must be idle" requirement, but idling first avoids racing a measurement
     * that might still be completing at the hardware level. */
    uint8_t err = ch_set_mode(dev, CH_MODE_IDLE);

    /* Must run even though no wire opcode explicitly asked for it - the transmitter never sees
     * individual AT_MSG_CALL opcodes, only whole snapshots, so this is the only way to avoid
     * stale segments from a previous (larger) snapshot accumulating. */
    err |= ch_meas_reset(dev, snap->meas_num);

    ch_meas_config_t mc = {
        .odr         = (ch_odr_t)snap->odr,
        .meas_period = snap->meas_period,
        .mode        = (ch_meas_mode_t)snap->meas_mode,
    };
    err |= ch_meas_init(dev, snap->meas_num, &mc, NULL);
    err |= icu_gpt_algo_init(dev, &g_algo_cfg);

    for (uint8_t i = 0; i < snap->num_segments; i++) {
        const at_segment_t *s = &snap->segments[i];
        switch ((at_seg_type_t)s->seg_type) {
        case AT_SEG_TX:
            err |= ch_meas_add_segment_tx(dev, snap->meas_num, s->p0, s->p1, s->p2, s->int_enable);
            break;
        case AT_SEG_RX:
            err |= ch_meas_add_segment_rx(dev, snap->meas_num, s->p0, s->p1, s->p2, s->int_enable);
            break;
        case AT_SEG_COUNT:
            err |= ch_meas_add_segment_count(dev, snap->meas_num, s->p0, s->int_enable);
            break;
        }
    }
    err |= ch_meas_write_config(dev);

    icu_gpt_algo_config_t algo_cfg = {
        .ringdown_cancel_samples = snap->algo_ringdown_cancel_samples,
        .static_filter_samples   = snap->algo_static_filter_samples,
        .iq_output_format        = (ch_output_type_t)snap->algo_iq_output_format,
        .num_ranges              = snap->algo_num_ranges,
        .filter_update_interval  = snap->algo_filter_update_interval,
    };
    ch_thresholds_t ch_thresh;
    at_thresholds_to_ch(&snap->thresholds, &ch_thresh);
    err |= icu_gpt_algo_configure(dev, snap->meas_num, &algo_cfg, &ch_thresh);
    err |= ch_set_algo_config(dev, &g_algo_cfg);
    err |= ch_init_algo(dev);

    err |= ch_set_max_range(dev, snap->max_range_mm);

    /* Always hardcoded locally - the snapshot never carries a sensing mode, since the
     * transmitter and receiver always run structurally different modes (see at_protocol.h). */
    err |= ch_set_mode(dev, CH_MODE_TRIGGERED_TX_RX);

    if (err) {
        ESP_LOGW(TAG, "snapshot apply failed (err mask nonzero) - will retry on next snapshot");
    } else {
        ESP_LOGI(TAG, "applied config snapshot gen=%u, %u segment(s), max range %u mm", snap->generation,
                 snap->num_segments, snap->max_range_mm);
    }
}

static void apply_task(void *arg) {
    (void)arg;
    at_config_snapshot_t snap;
    for (;;) {
        xQueueReceive(g_snapshot_queue, &snap, portMAX_DELAY);
        apply_snapshot(g_dev, &snap);
    }
}

void at_transmitter_run(ch_group_t *grp, ch_dev_t *dev) {
    /* Unused beyond the parameter itself: this app never registers a group-level data-ready
     * callback or triggers - the sensor still transmits and listens for its own echo internally
     * (CH_MODE_TRIGGERED_TX_RX requires an RX segment to run at all), and the BSP still services
     * INT2 unconditionally at the driver level, keeping it synchronized/re-armed, even with no
     * app-level callback registered - see pitch_catch_mode_hardware_test's sender_loop.c. */
    (void)grp;
    g_dev = dev;

    g_snapshot_queue = xQueueCreate(1, sizeof(at_config_snapshot_t));
    if (g_snapshot_queue == NULL) {
        ESP_LOGE(TAG, "queue alloc failed - halting");
        for (;;) {
            vTaskDelay(portMAX_DELAY);
        }
    }

    ESP_ERROR_CHECK(espnow_link_init(g_snapshot_queue));

    xTaskCreate(apply_task, "at_apply", 4096, NULL, 5, NULL);

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(10000));
        ESP_LOGI(TAG, "idle - awaiting ESP-NOW config snapshots");
    }
}
