/*! \file rangefinder_loop.c
 *
 * \brief Implementation of the bring-up rangefinding loop. See rangefinder_loop.h.
 *
 * Flow: build a measurement queue (transmit -> settle -> receive), configure the icu_gpt
 * rangefinding algorithm + detection thresholds, set 5 m full-scale range and the free-run
 * interval, switch to CH_MODE_FREERUN, then print each measured distance as the sensor's
 * data-ready callback delivers it (the BSP runs that callback from bsp_int_task, task level).
 */

#include "rangefinder_loop.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"

#include <invn/soniclib/soniclib.h>
#include <invn/soniclib/sensor_fw/icu_gpt/icu_gpt.h> /* icu_gpt_algo_*, InvnAlgoRangeFinderConfig, ch_thresholds_t */

static const char *TAG = "rangefinder";

/* ============================ Application configuration ============================ *
 * The values below are bring-up starting points for the ICU-20201 with icu_gpt firmware.
 * The range/interval are per the requirement; the transmit/receive/threshold values will
 * likely need tuning on the bench - see the note on each.                              */

#define RF_MAX_RANGE_MM        5000u /* one-way full-scale range = 5 m (requirement) */
#define RF_FREERUN_INTERVAL_MS 100u  /* 10 Hz. Must exceed the measurement time: roughly
                                      * 6 ms per metre of round-trip listening + ~2 ms
                                      * processing => ~32 ms at 5 m. 100 ms leaves margin. */

#define RF_ODR CH_ODR_DEFAULT /* fop/8 - DS-000478: medium ODR for rangefinding up to 5 m */

/* Transmit segment - the outgoing ultrasound burst. */
#define RF_TX_PULSE_US    80u /* burst duration. Longer = more energy/range but more ringdown.
                              * Raise if far targets give no echo; lower if close-in is blinded. */
#define RF_TX_PULSE_WIDTH 3u  /* drive pulse width, 0-7 */
#define RF_TX_PHASE       8u  /* drive phase, 0-15 */

/* Count (delay) segment after transmit, to let the transducer settle before receiving. */
#define RF_SETTLE_US 100u

/* Receive segment. */
#define RF_RX_GAIN_REDUCE 0u /* 0 = maximum receiver gain (0-31). Raise if near-field noise
                              * produces false targets. */
#define RF_RX_ATTEN       0u /* receiver attenuation, 0-3 */

/* icu_gpt rangefinding algorithm configuration. */
#define RF_RINGDOWN_CANCEL_SAMPLES 20u /* filter transducer ringdown over the first N samples
                                        * (<= RINGDOWN_CANCEL_SAMPLES_MAX). */
#define RF_STATIC_FILTER_SAMPLES   0u  /* 0 = static target rejection off */
#define RF_NUM_RANGES              1u  /* report the closest target only */

/* Detection thresholds: the CORDIC magnitude a sample must exceed to be a target, stepping
 * down for farther (later) samples. The first entry must start at sample 0; unused trailing
 * entries are {0, 0}. Tune: raise levels if false targets appear, lower if real ones are
 * missed. (icu_gpt supports up to CH_NUM_THRESHOLDS entries.) */
static const ch_thresholds_t rf_thresholds = {
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

static const icu_gpt_algo_config_t rf_gpt_cfg = {
    .ringdown_cancel_samples = RF_RINGDOWN_CANCEL_SAMPLES,
    .static_filter_samples   = RF_STATIC_FILTER_SAMPLES,
    .iq_output_format        = CH_OUTPUT_IQ,
    .num_ranges              = RF_NUM_RANGES,
    .filter_update_interval  = 0,
};

/* Raw per-measurement algorithm config the sensor receives; filled by icu_gpt_algo_configure(). */
static InvnAlgoRangeFinderConfig rf_algo_cfg;

/* ============================ Data-ready plumbing ============================ */

typedef struct {
    bool     have_target;
    uint32_t range_q5; /* ch_get_range() units: millimetres * 32 */
    uint16_t amplitude;
} rf_sample_t;

static QueueHandle_t rf_queue; /* length 1, overwrite semantics - always the latest sample */

/* Called by SonicLib after each measurement. Runs in the BSP's bsp_int_task (task level), so
 * blocking SPI reads here are fine. Keep it short: read the result, hand it to the printer. */
static void rf_data_ready_cb(ch_group_t *grp, uint8_t io_index, ch_interrupt_type_t int_type) {
    if (int_type != CH_INTERRUPT_TYPE_DATA_RDY) {
        return; /* ignore program-loaded / error / other interrupt types */
    }

    ch_dev_t *dev  = ch_get_dev_ptr(grp, io_index);
    uint32_t  range = ch_get_range(dev, CH_RANGE_ECHO_ONE_WAY);

    rf_sample_t s = {
        .have_target = (range != CH_NO_TARGET),
        .range_q5    = range,
        .amplitude   = (range != CH_NO_TARGET) ? ch_get_amplitude(dev) : 0,
    };
    xQueueOverwrite(rf_queue, &s);
}

/* ============================ Configuration ============================ */

static int rf_configure(ch_dev_t *dev) {
    uint8_t err = 0;

    ch_meas_config_t meas_cfg = {
        .odr         = RF_ODR,
        .meas_period = 0, /* free-run interval is set below via ch_set_freerun_interval() */
        .mode        = CH_MEAS_MODE_ACTIVE,
    };
    err |= ch_meas_init(dev, CH_DEFAULT_MEAS_NUM, &meas_cfg, NULL);

    /* GPT rangefinding algorithm + thresholds */
    err |= icu_gpt_algo_init(dev, &rf_algo_cfg);
    err |= icu_gpt_algo_configure(dev, CH_DEFAULT_MEAS_NUM, &rf_gpt_cfg, &rf_thresholds);

    /* Measurement queue: transmit burst -> settle -> receive (interrupt on the final segment) */
    uint16_t tx_cycles     = (uint16_t)ch_usec_to_cycles(dev, RF_TX_PULSE_US);
    uint16_t settle_cycles = (uint16_t)ch_usec_to_cycles(dev, RF_SETTLE_US);
    err |= ch_meas_add_segment_tx(dev, CH_DEFAULT_MEAS_NUM, tx_cycles, RF_TX_PULSE_WIDTH, RF_TX_PHASE, 0);
    err |= ch_meas_add_segment_count(dev, CH_DEFAULT_MEAS_NUM, settle_cycles, 0);
    err |= ch_meas_add_segment_rx(dev, CH_DEFAULT_MEAS_NUM, ICU_MAX_NUM_SAMPLES, RF_RX_GAIN_REDUCE, RF_RX_ATTEN,
                                  1 /* done interrupt on the last RX segment */);
    err |= ch_meas_write_config(dev);

    /* Push the algorithm configuration to the sensor */
    err |= ch_set_algo_config(dev, &rf_algo_cfg);
    err |= ch_init_algo(dev);

    /* High-level settings: 5 m range trims the RX sample count to what's needed; then interval;
     * ch_set_mode() must be last - it starts sensing. */
    err |= ch_set_max_range(dev, RF_MAX_RANGE_MM);
    err |= ch_set_freerun_interval(dev, RF_FREERUN_INTERVAL_MS);
    err |= ch_set_mode(dev, CH_MODE_FREERUN);

    return err ? -1 : 0;
}

/* ============================ Public entry point ============================ */

void rangefinder_run(ch_group_t *grp, ch_dev_t *dev) {
    rf_queue = xQueueCreate(1, sizeof(rf_sample_t));
    if (rf_queue == NULL) {
        ESP_LOGE(TAG, "queue alloc failed - halting");
        for (;;) {
            vTaskDelay(portMAX_DELAY);
        }
    }

    ch_io_int_callback_set(grp, rf_data_ready_cb);

    ESP_LOGI(TAG, "configuring: free-run, max range %u mm, interval %u ms", RF_MAX_RANGE_MM,
             RF_FREERUN_INTERVAL_MS);
    if (rf_configure(dev) != 0) {
        ESP_LOGE(TAG, "sensor configuration failed - halting");
        for (;;) {
            vTaskDelay(portMAX_DELAY);
        }
    }
    ESP_LOGI(TAG, "running: op freq %" PRIu32 " Hz, %u active samples, max range %u mm",
             ch_get_frequency(dev), ch_get_num_samples(dev), ch_get_max_range(dev));

    const TickType_t timeout = pdMS_TO_TICKS(RF_FREERUN_INTERVAL_MS * 5);
    rf_sample_t      s;
    uint32_t         n = 0;

    for (;;) {
        if (xQueueReceive(rf_queue, &s, timeout) != pdTRUE) {
            ESP_LOGW(TAG, "no data-ready for %u ms - check INT2 wiring / sensor state",
                     (unsigned)(RF_FREERUN_INTERVAL_MS * 5));
            continue;
        }
        n++;
        if (s.have_target) {
            ESP_LOGI(TAG, "#%" PRIu32 "  %8.1f mm   (amp %u)", n, s.range_q5 / 32.0f, s.amplitude);
        } else {
            ESP_LOGI(TAG, "#%" PRIu32 "  no target", n);
        }
    }
}
