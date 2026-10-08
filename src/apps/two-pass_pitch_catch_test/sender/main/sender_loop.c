/*! \file sender_loop.c
 *
 * \brief Implementation of the bring-up pitch-catch sender. See sender_loop.h.
 *
 * Flow: build a measurement queue (transmit -> settle -> receive), configure the icu_gpt
 * rangefinding algorithm + detection thresholds, set 5 m full-scale range, switch to
 * CH_MODE_TRIGGERED_TX_RX, then idle forever.
 *
 * CH_MODE_TRIGGERED_TX_RX still transmits *and* listens for its own echo (AN-000175: "the
 * transmitting sensor will still listen for its echo and report the results ... operates the
 * same as when it is a single sensor operating independently") - that RX segment is required for
 * the sensor to run at all, but this app deliberately ignores the result: it never calls
 * ch_io_int_callback_set(), so no app-level callback ever reads ch_get_range()/amplitude here.
 * The BSP still services the sensor's INT2 (data-ready) line unconditionally at the driver level
 * (bsp_int_task -> ch_interrupt()) regardless of whether an app callback is registered - that is
 * what keeps the sensor synchronized/re-armed for the next trigger, and it's the same physical
 * toggling the receiver's GPIO33 sender-ready tap observes (see pitch_catch_common.h).
 *
 * This app must never call ch_trigger()/ch_group_trigger() - the receiver board owns the
 * periodic trigger loop and fires this sensor externally over the shared INT1 line.
 */

#include "sender_loop.h"

#include <inttypes.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include <invn/soniclib/soniclib.h>
#include <invn/soniclib/sensor_fw/icu_gpt/icu_gpt.h> /* icu_gpt_algo_*, InvnAlgoRangeFinderConfig, ch_thresholds_t */

#include "pitch_catch_common.h" /* PC_TX_PULSE_US, PC_TX_PULSE_WIDTH, PC_TX_PHASE, PC_ODR, PC_MAX_RANGE_MM */

static const char *TAG = "pitch-catch-tx";

/* ============================ Application configuration ============================ *
 * PC_TX_* / PC_ODR / PC_MAX_RANGE_MM come from pitch_catch_common.h - they must match the
 * receiver's expectations. The values below are local: only this sensor's own (unused-by-the-
 * app) settle/RX timing depends on them, so they don't need to match the receiver. Bring-up
 * starting points; bench tuning is future work - see TODO.md. */

#define TX_SETTLE_US    100u /* delay segment after transmit, to let the transducer settle */
#define TX_RX_GAIN_REDUCE 0u /* the sensor's own RX segment still needs a valid gain config even
                              * though the app never reads the result */
#define TX_RX_ATTEN       0u

#define TX_RINGDOWN_CANCEL_SAMPLES 20u
#define TX_STATIC_FILTER_SAMPLES   0u
#define TX_NUM_RANGES              1u

/* Unused by the app (no callback reads range here), but icu_gpt_algo_configure() requires a
 * valid threshold table for the algorithm to run. Same starting values as echo_mode_hardware_test. */
static const ch_thresholds_t tx_thresholds = {
    .threshold = {
        {.start_sample = 0, .level = 2500},
        {.start_sample = 30, .level = 1200},
        {.start_sample = 60, .level = 700},
        {.start_sample = 120, .level = 450},
        {.start_sample = 200, .level = 350},
        {.start_sample = 0, .level = 0},
        {.start_sample = 0, .level = 0},
        {.start_sample = 0, .level = 0},
    },
};

static const icu_gpt_algo_config_t tx_gpt_cfg = {
    .ringdown_cancel_samples = TX_RINGDOWN_CANCEL_SAMPLES,
    .static_filter_samples   = TX_STATIC_FILTER_SAMPLES,
    .iq_output_format        = CH_OUTPUT_IQ,
    .num_ranges              = TX_NUM_RANGES,
    .filter_update_interval  = 0,
};

/* Raw per-measurement algorithm config the sensor receives; filled by icu_gpt_algo_configure(). */
static InvnAlgoRangeFinderConfig tx_algo_cfg;

/* ============================ Configuration ============================ */

static int sender_configure(ch_dev_t *dev) {
    uint8_t err = 0;

    ch_meas_config_t meas_cfg = {
        .odr         = PC_ODR,
        .meas_period = 0, /* not used in triggered mode */
        .mode        = CH_MEAS_MODE_ACTIVE,
    };
    err |= ch_meas_init(dev, CH_DEFAULT_MEAS_NUM, &meas_cfg, NULL);

    /* GPT rangefinding algorithm + thresholds - required for the sensor to run, even though the
     * app never consumes the results on this board. */
    err |= icu_gpt_algo_init(dev, &tx_algo_cfg);
    err |= icu_gpt_algo_configure(dev, CH_DEFAULT_MEAS_NUM, &tx_gpt_cfg, &tx_thresholds);

    /* Measurement queue: transmit burst -> settle -> receive (interrupt on the final segment).
     * PC_TX_PULSE_US/WIDTH/PHASE are shared with the receiver's count-segment sizing - see
     * pitch_catch_common.h. */
    uint16_t tx_cycles     = (uint16_t)ch_usec_to_cycles(dev, PC_TX_PULSE_US);
    uint16_t settle_cycles = (uint16_t)ch_usec_to_cycles(dev, TX_SETTLE_US);
    err |= ch_meas_add_segment_tx(dev, CH_DEFAULT_MEAS_NUM, tx_cycles, PC_TX_PULSE_WIDTH, PC_TX_PHASE, 0);
    err |= ch_meas_add_segment_count(dev, CH_DEFAULT_MEAS_NUM, settle_cycles, 0);
    err |= ch_meas_add_segment_rx(dev, CH_DEFAULT_MEAS_NUM, ICU_MAX_NUM_SAMPLES, TX_RX_GAIN_REDUCE, TX_RX_ATTEN,
                                  1 /* done interrupt on the last RX segment */);
    err |= ch_meas_write_config(dev);

    /* Push the algorithm configuration to the sensor */
    err |= ch_set_algo_config(dev, &tx_algo_cfg);
    err |= ch_init_algo(dev);

    /* High-level settings; ch_set_mode() must be last - it starts sensing (in this case, arms
     * the sensor to react to the next externally-driven INT1 trigger edge). */
    err |= ch_set_max_range(dev, PC_MAX_RANGE_MM);
    err |= ch_set_mode(dev, CH_MODE_TRIGGERED_TX_RX);

    return err ? -1 : 0;
}

/* ============================ Public entry point ============================ */

void sender_run(ch_group_t *grp, ch_dev_t *dev) {
    (void)grp; /* unused: this app never registers a group-level data-ready callback */

    ESP_LOGI(TAG, "configuring: triggered TX/RX, max range %u mm", PC_MAX_RANGE_MM);
    if (sender_configure(dev) != 0) {
        ESP_LOGE(TAG, "sensor configuration failed - halting");
        for (;;) {
            vTaskDelay(portMAX_DELAY);
        }
    }
    ESP_LOGI(TAG, "configured: op freq %" PRIu32 " Hz, %u active samples, max range %u mm - idling, "
             "awaiting external triggers over shared INT1",
             ch_get_frequency(dev), ch_get_num_samples(dev), ch_get_max_range(dev));

    /* Idle forever. This app never calls ch_trigger()/ch_group_trigger() (the receiver owns the
     * trigger loop) and never reads its own measurement data (ch_io_int_callback_set() is never
     * called) - a slow heartbeat log is the only console activity, just to confirm the board is
     * alive on the bench. */
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(10000));
        ESP_LOGI(TAG, "idle - awaiting external triggers");
    }
}
