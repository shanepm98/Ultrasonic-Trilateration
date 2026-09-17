#include "at_sensor_config.h"

#include <stddef.h>
#include <string.h>

/* icu_gpt.h must come before ch_rangefinder.h: it includes invn/icu_interface/ch-rangefinder/
 * structs.h (defines LEN_THRESH) ahead of ch_rangefinder_types.h (which needs it via
 * CH_NUM_THRESHOLDS) - including ch_rangefinder.h first hits that header directly, too early. */
#include <invn/soniclib/sensor_fw/icu_gpt/icu_gpt.h> /* icu_gpt_algo_*, InvnAlgoRangeFinderConfig */
#include <invn/soniclib/ch_rangefinder.h>             /* ch_set_thresholds, ch_get_thresholds */

#include "at_common.h"
#include "espnow_relay.h"

static ch_dev_t                  *g_dev;
static InvnAlgoRangeFinderConfig  g_algo_cfg;

/* Staged state, mirroring exactly what's been applied to the local sensor via the opcode
 * handlers below. Copied into an at_config_snapshot_t and pushed over ESP-NOW whenever
 * at_cfg_set_mode() runs (the end of every reconfigure sequence). Single-writer (the at_rpc
 * task, via rpc_dispatch.c) - no locking needed for these fields themselves. */
static at_segment_t                 staged_segments[AT_MAX_SEGMENTS];
static uint8_t                      staged_num_segments;
static at_call_meas_init_t          staged_meas_init;
static uint16_t                     staged_max_range_mm;
static at_call_gpt_algo_configure_t staged_algo;
static at_thresholds_t              staged_thresholds; /* latest of {SET_THRESHOLDS, GPT_ALGO_CONFIGURE} wins -
                                                          * both write the same underlying sensor threshold table */
static uint8_t                      g_generation;

static bool              g_reconfiguring;
static bool              g_config_ever_ok;
static SemaphoreHandle_t g_state_mutex;
static SemaphoreHandle_t g_trigger_cycle_mutex;

static void at_thresholds_to_ch(const at_thresholds_t *in, ch_thresholds_t *out) {
    for (int i = 0; i < (int)AT_NUM_THRESHOLDS; i++) {
        out->threshold[i].start_sample = in->threshold[i].start_sample;
        out->threshold[i].level        = in->threshold[i].level;
    }
}

static void ch_thresholds_to_at(const ch_thresholds_t *in, at_thresholds_t *out) {
    for (int i = 0; i < (int)AT_NUM_THRESHOLDS; i++) {
        out->threshold[i].start_sample = in->threshold[i].start_sample;
        out->threshold[i].level        = in->threshold[i].level;
    }
}

void at_sensor_config_init(ch_dev_t *dev) {
    g_dev = dev;
    memset(&g_algo_cfg, 0, sizeof(g_algo_cfg));

    staged_num_segments = 0;
    memset(&staged_meas_init, 0, sizeof(staged_meas_init));
    staged_max_range_mm = 0;
    memset(&staged_algo, 0, sizeof(staged_algo));
    memset(&staged_thresholds, 0, sizeof(staged_thresholds));
    g_generation = 0;

    g_reconfiguring  = false;
    g_config_ever_ok = false;
    g_state_mutex         = xSemaphoreCreateMutex();
    g_trigger_cycle_mutex = xSemaphoreCreateMutex();
}

SemaphoreHandle_t at_sensor_trigger_cycle_mutex(void) {
    return g_trigger_cycle_mutex;
}

bool at_sensor_ready_to_trigger(void) {
    xSemaphoreTake(g_state_mutex, portMAX_DELAY);
    bool ready = g_config_ever_ok && !g_reconfiguring;
    xSemaphoreGive(g_state_mutex);
    return ready;
}

static void at_push_config_snapshot(void) {
    at_config_snapshot_t snap;
    memset(&snap, 0, sizeof(snap));

    snap.proto_version = AT_PROTOCOL_VERSION;
    snap.generation     = g_generation++;
    snap.meas_num       = staged_meas_init.meas_num;
    snap.odr            = staged_meas_init.odr;
    snap.meas_period    = staged_meas_init.meas_period;
    snap.meas_mode      = staged_meas_init.mode;
    snap.num_segments   = staged_num_segments;
    memcpy(snap.segments, staged_segments, sizeof(staged_segments));
    snap.max_range_mm                   = staged_max_range_mm;
    snap.algo_ringdown_cancel_samples   = staged_algo.ringdown_cancel_samples;
    snap.algo_static_filter_samples     = staged_algo.static_filter_samples;
    snap.algo_iq_output_format          = staged_algo.iq_output_format;
    snap.algo_num_ranges                = staged_algo.num_ranges;
    snap.algo_filter_update_interval    = staged_algo.filter_update_interval;
    snap.thresholds                     = staged_thresholds;
    snap.crc16 = at_crc16((const uint8_t *)&snap, offsetof(at_config_snapshot_t, crc16));

    espnow_relay_send(&snap, sizeof(snap));
}

/* ===================== Opcode handlers ===================== */

at_status_t at_cfg_hello(const uint8_t *body, uint8_t *resp, uint8_t *resp_len) {
    (void)body;
    at_resp_hello_t r = { .version = AT_PROTOCOL_VERSION };
    memcpy(resp, &r, sizeof(r));
    *resp_len = sizeof(r);
    return AT_STATUS_OK;
}

at_status_t at_cfg_meas_reset(const uint8_t *body, uint8_t *resp, uint8_t *resp_len) {
    const at_call_meas_reset_t *b = (const at_call_meas_reset_t *)body;
    (void)resp;
    *resp_len = 0;

    xSemaphoreTake(g_state_mutex, portMAX_DELAY);
    g_reconfiguring = true;
    xSemaphoreGive(g_state_mutex);

    /* Block until any in-flight trigger+response-wait cycle finishes, so this reconfigure can
     * never race a measurement already underway - closes the race rather than just gating
     * future triggers with a flag. Bounded wait: the trigger loop never holds this mutex longer
     * than its own response timeout. */
    xSemaphoreTake(g_trigger_cycle_mutex, pdMS_TO_TICKS(AT_RESPONSE_TIMEOUT_MS + 20));

    /* Defensive bracket: SonicLib documents no "must be idle" requirement for ch_meas_reset(),
     * but the underlying implementation has no busy-check either, so idling first avoids racing
     * a measurement that might still be completing at the hardware level. */
    uint8_t err = ch_set_mode(g_dev, CH_MODE_IDLE);
    err |= ch_meas_reset(g_dev, b->meas_num);
    staged_num_segments = 0;

    xSemaphoreGive(g_trigger_cycle_mutex);

    return err ? AT_STATUS_ERR : AT_STATUS_OK;
}

at_status_t at_cfg_meas_init(const uint8_t *body, uint8_t *resp, uint8_t *resp_len) {
    const at_call_meas_init_t *b = (const at_call_meas_init_t *)body;
    (void)resp;
    *resp_len = 0;

    staged_meas_init = *b;

    ch_meas_config_t cfg = {
        .odr         = (ch_odr_t)b->odr,
        .meas_period = b->meas_period,
        .mode        = (ch_meas_mode_t)b->mode,
    };
    uint8_t err = ch_meas_init(g_dev, b->meas_num, &cfg, NULL);
    err |= icu_gpt_algo_init(g_dev, &g_algo_cfg);

    return err ? AT_STATUS_ERR : AT_STATUS_OK;
}

/* Stages a segment into staged_segments[] for ESP-NOW relay to the transmitter. Never touches
 * g_dev - staged_segments[] exclusively describes the transmitter's queue (see the target-field
 * design note next to at_call_add_segment_tx_t in at_protocol.h). Returns AT_STATUS_ERR without
 * staging if AT_MAX_SEGMENTS is already reached. */
static at_status_t stage_remote_segment(at_seg_type_t type, uint16_t p0, uint8_t p1, uint8_t p2,
                                         uint8_t int_enable) {
    if (staged_num_segments >= AT_MAX_SEGMENTS) {
        return AT_STATUS_ERR;
    }
    at_segment_t *s = &staged_segments[staged_num_segments++];
    s->seg_type   = type;
    s->int_enable = int_enable;
    s->p0         = p0;
    s->p1         = p1;
    s->p2         = p2;
    return AT_STATUS_OK;
}

at_status_t at_cfg_add_segment_tx(const uint8_t *body, uint8_t *resp, uint8_t *resp_len) {
    const at_call_add_segment_tx_t *b = (const at_call_add_segment_tx_t *)body;
    (void)resp;
    *resp_len = 0;

    if (b->target == AT_SEG_TARGET_LOCAL) {
        /* The receiver is always RX-only in this app's fixed pitch-catch role - a TX segment
         * never belongs on its own sensor. Reject rather than silently misapplying it. */
        return AT_STATUS_ERR;
    }
    return stage_remote_segment(AT_SEG_TX, b->num_cycles, b->pulse_width, b->phase, b->int_enable);
}

at_status_t at_cfg_add_segment_rx(const uint8_t *body, uint8_t *resp, uint8_t *resp_len) {
    const at_call_add_segment_rx_t *b = (const at_call_add_segment_rx_t *)body;
    (void)resp;
    *resp_len = 0;

    if (b->target == AT_SEG_TARGET_REMOTE) {
        return stage_remote_segment(AT_SEG_RX, b->num_samples, b->gain_reduce, b->atten, b->int_enable);
    }
    /* AT_SEG_TARGET_LOCAL: apply directly to the receiver's own sensor. Not staged - the
     * receiver's local queue and the transmitter's relayed queue are independent (COUNT+RX here
     * vs TX+COUNT+RX there), see at_protocol.h's target-field design note. */
    uint8_t err = ch_meas_add_segment_rx(g_dev, b->meas_num, b->num_samples, b->gain_reduce, b->atten,
                                          b->int_enable);
    return err ? AT_STATUS_ERR : AT_STATUS_OK;
}

at_status_t at_cfg_add_segment_count(const uint8_t *body, uint8_t *resp, uint8_t *resp_len) {
    const at_call_add_segment_count_t *b = (const at_call_add_segment_count_t *)body;
    (void)resp;
    *resp_len = 0;

    if (b->target == AT_SEG_TARGET_REMOTE) {
        return stage_remote_segment(AT_SEG_COUNT, b->num_cycles, 0, 0, b->int_enable);
    }
    uint8_t err = ch_meas_add_segment_count(g_dev, b->meas_num, b->num_cycles, b->int_enable);
    return err ? AT_STATUS_ERR : AT_STATUS_OK;
}

at_status_t at_cfg_meas_write_config(const uint8_t *body, uint8_t *resp, uint8_t *resp_len) {
    (void)body;
    (void)resp;
    *resp_len = 0;
    uint8_t err = ch_meas_write_config(g_dev);
    return err ? AT_STATUS_ERR : AT_STATUS_OK;
}

at_status_t at_cfg_set_odr(const uint8_t *body, uint8_t *resp, uint8_t *resp_len) {
    const at_call_set_odr_t *b = (const at_call_set_odr_t *)body;
    (void)resp;
    *resp_len = 0;
    uint8_t err = ch_meas_set_odr(g_dev, b->meas_num, (ch_odr_t)b->odr);
    return err ? AT_STATUS_ERR : AT_STATUS_OK;
}

at_status_t at_cfg_set_num_samples(const uint8_t *body, uint8_t *resp, uint8_t *resp_len) {
    const at_call_set_num_samples_t *b = (const at_call_set_num_samples_t *)body;
    (void)resp;
    *resp_len = 0;
    uint8_t err = ch_meas_set_num_samples(g_dev, b->meas_num, b->num_samples);
    return err ? AT_STATUS_ERR : AT_STATUS_OK;
}

at_status_t at_cfg_set_max_range_meas(const uint8_t *body, uint8_t *resp, uint8_t *resp_len) {
    const at_call_set_max_range_meas_t *b = (const at_call_set_max_range_meas_t *)body;
    (void)resp;
    *resp_len = 0;
    uint8_t err = ch_meas_set_max_range(g_dev, b->meas_num, b->max_range_mm);
    return err ? AT_STATUS_ERR : AT_STATUS_OK;
}

at_status_t at_cfg_set_max_range(const uint8_t *body, uint8_t *resp, uint8_t *resp_len) {
    const at_call_set_max_range_t *b = (const at_call_set_max_range_t *)body;
    (void)resp;
    *resp_len = 0;
    uint8_t err = ch_set_max_range(g_dev, b->max_range_mm);
    if (err == 0) {
        staged_max_range_mm = b->max_range_mm;
    }
    return err ? AT_STATUS_ERR : AT_STATUS_OK;
}

at_status_t at_cfg_set_thresholds(const uint8_t *body, uint8_t *resp, uint8_t *resp_len) {
    const at_call_set_thresholds_t *b = (const at_call_set_thresholds_t *)body; /* == at_thresholds_t */
    (void)resp;
    *resp_len = 0;

    ch_thresholds_t ch_thresh;
    at_thresholds_to_ch(b, &ch_thresh);
    uint8_t err = ch_set_thresholds(g_dev, &ch_thresh);
    if (err == 0) {
        staged_thresholds = *b;
    }
    return err ? AT_STATUS_ERR : AT_STATUS_OK;
}

at_status_t at_cfg_get_thresholds(const uint8_t *body, uint8_t *resp, uint8_t *resp_len) {
    (void)body;
    ch_thresholds_t ch_thresh;
    uint8_t err = ch_get_thresholds(g_dev, &ch_thresh);
    if (err == 0) {
        at_resp_get_thresholds_t r; /* == at_thresholds_t */
        ch_thresholds_to_at(&ch_thresh, &r);
        memcpy(resp, &r, sizeof(r));
        *resp_len = sizeof(r);
    } else {
        *resp_len = 0;
    }
    return err ? AT_STATUS_ERR : AT_STATUS_OK;
}

at_status_t at_cfg_gpt_algo_configure(const uint8_t *body, uint8_t *resp, uint8_t *resp_len) {
    const at_call_gpt_algo_configure_t *b = (const at_call_gpt_algo_configure_t *)body;
    (void)resp;
    *resp_len = 0;

    staged_algo       = *b;
    staged_thresholds = b->thresholds; /* latest of {SET_THRESHOLDS, GPT_ALGO_CONFIGURE} wins */

    icu_gpt_algo_config_t cfg = {
        .ringdown_cancel_samples = b->ringdown_cancel_samples,
        .static_filter_samples   = b->static_filter_samples,
        .iq_output_format        = (ch_output_type_t)b->iq_output_format,
        .num_ranges              = b->num_ranges,
        .filter_update_interval  = b->filter_update_interval,
    };
    ch_thresholds_t ch_thresh;
    at_thresholds_to_ch(&b->thresholds, &ch_thresh);

    uint8_t err = icu_gpt_algo_configure(g_dev, b->meas_num, &cfg, &ch_thresh);
    err |= ch_set_algo_config(g_dev, &g_algo_cfg);
    err |= ch_init_algo(g_dev);

    return err ? AT_STATUS_ERR : AT_STATUS_OK;
}

at_status_t at_cfg_set_mode(const uint8_t *body, uint8_t *resp, uint8_t *resp_len) {
    const at_call_set_mode_t *b = (const at_call_set_mode_t *)body;
    (void)resp;
    *resp_len = 0;

    uint8_t err = ch_set_mode(g_dev, (ch_mode_t)b->mode);

    /* Unconditional: fires regardless of success/failure, so one bad reconfigure doesn't
     * permanently silence telemetry on a live bring-up bench (matches at_protocol.h's design -
     * the ESP-NOW link tolerates a stale/dropped snapshot by design). */
    at_push_config_snapshot();

    xSemaphoreTake(g_state_mutex, portMAX_DELAY);
    if (err == 0) {
        g_config_ever_ok = true;
    }
    g_reconfiguring = false;
    xSemaphoreGive(g_state_mutex);

    return err ? AT_STATUS_ERR : AT_STATUS_OK;
}

at_status_t at_cfg_get_status(const uint8_t *body, uint8_t *resp, uint8_t *resp_len) {
    (void)body;
    at_resp_get_status_t r = {
        .op_freq_hz   = ch_get_frequency(g_dev),
        .num_samples  = ch_get_num_samples(g_dev),
        .max_range_mm = ch_get_max_range(g_dev),
    };
    memcpy(resp, &r, sizeof(r));
    *resp_len = sizeof(r);
    return AT_STATUS_OK;
}
