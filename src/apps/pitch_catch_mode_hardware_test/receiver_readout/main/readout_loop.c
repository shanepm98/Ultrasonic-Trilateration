/*! \file readout_loop.c
 *
 * \brief Implementation of the pitch-catch receiver's raw I/Q readout variant. See readout_loop.h.
 *
 * Sensor configuration (measurement queue, icu_gpt algorithm + thresholds, max range, triggered
 * RX-only mode) is identical to receiver/main/receiver_loop.c's receiver_configure() - this app
 * only changes what happens after a trigger: instead of a fixed 10 Hz loop reporting the computed
 * distance, it idles for a line typed on the serial console, fires exactly one trigger, and dumps
 * that measurement's full raw I/Q trace as plain text. A ~1.4 KB dump (up to ICU_MAX_NUM_SAMPLES
 * samples x 4 bytes) doesn't fit in a 100 ms trigger interval over a typical console baud rate,
 * so this variant is on-demand rather than free-running - see ../../README.md.
 */

#include "readout_loop.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "driver/gpio.h"

#include <invn/soniclib/soniclib.h>
#include <invn/soniclib/sensor_fw/icu_gpt/icu_gpt.h> /* icu_gpt_algo_*, InvnAlgoRangeFinderConfig, ch_thresholds_t */

#include "pitch_catch_common.h" /* PC_TX_PULSE_US, PC_ODR, PC_MAX_RANGE_MM, PC_RESPONSE_TIMEOUT_MS,
                                  * PC_SENDER_READY_GPIO */

static const char *TAG = "pitch-catch-rx-readout";

/* ============================ Application configuration ============================ *
 * Same values as receiver/main/receiver_loop.c - this RX-only sensor's own noise floor/ringdown
 * is independently tunable from the sender's, and identical starting points make the two apps'
 * output directly comparable on the bench. */

#define RX_GAIN_REDUCE 0u
#define RX_ATTEN       0u

#define RX_RINGDOWN_CANCEL_SAMPLES 20u
#define RX_STATIC_FILTER_SAMPLES   0u
#define RX_NUM_RANGES              1u

static const ch_thresholds_t rx_thresholds = {
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

static const icu_gpt_algo_config_t rx_gpt_cfg = {
    .ringdown_cancel_samples = RX_RINGDOWN_CANCEL_SAMPLES,
    .static_filter_samples   = RX_STATIC_FILTER_SAMPLES,
    .iq_output_format        = CH_OUTPUT_IQ, /* required for ch_get_iq_data() to return valid data */
    .num_ranges              = RX_NUM_RANGES,
    .filter_update_interval  = 0,
};

/* Raw per-measurement algorithm config the sensor receives; filled by icu_gpt_algo_configure(). */
static InvnAlgoRangeFinderConfig rx_algo_cfg;

/* ============================ Data-ready plumbing ============================ */

typedef struct {
    bool     have_target;
    uint32_t range_q5; /* ch_get_range() units: millimetres * 32 */
    uint16_t amplitude;
    uint16_t num_samples;
    uint8_t  iq_err; /* ch_get_iq_data() return code; 0 = OK */
} readout_summary_t;

/* Only one measurement is ever in flight at a time (on-demand triggering), so a single static
 * buffer + summary is enough - no need for the receiver/'s length-1 overwrite queue. */
static ch_iq_sample_t     iq_buf[ICU_MAX_NUM_SAMPLES];
static readout_summary_t  summary;
static SemaphoreHandle_t  data_ready_sem;

/* Called by SonicLib after each measurement. Runs in the BSP's bsp_int_task (task level), so
 * blocking SPI reads here - including the I/Q readout - are fine. */
static void readout_data_ready_cb(ch_group_t *grp, uint8_t io_index, ch_interrupt_type_t int_type) {
    if (int_type != CH_INTERRUPT_TYPE_DATA_RDY) {
        return; /* ignore program-loaded / error / other interrupt types */
    }

    ch_dev_t *dev   = ch_get_dev_ptr(grp, io_index);
    uint32_t  range = ch_get_range(dev, CH_RANGE_DIRECT); /* direct pitch-catch: separate objects */

    summary.have_target = (range != CH_NO_TARGET);
    summary.range_q5    = range;
    summary.amplitude   = summary.have_target ? ch_get_amplitude(dev) : 0;
    summary.num_samples = ch_get_num_samples(dev);
    if (summary.num_samples > ICU_MAX_NUM_SAMPLES) {
        summary.num_samples = ICU_MAX_NUM_SAMPLES; /* defensive; shouldn't happen at PC_MAX_RANGE_MM */
    }

    summary.iq_err = ch_get_iq_data(dev, iq_buf, 0, summary.num_samples, CH_IO_MODE_BLOCK);

    xSemaphoreGive(data_ready_sem);
}

/* ============================ Configuration ============================ *
 * Identical to receiver/main/receiver_loop.c's receiver_configure() - see that file / the shared
 * README for the segment-timing rationale. */

static int readout_configure(ch_dev_t *dev) {
    uint8_t err = 0;

    ch_meas_config_t meas_cfg = {
        .odr         = PC_ODR,
        .meas_period = 0, /* not used in triggered mode */
        .mode        = CH_MEAS_MODE_ACTIVE,
    };
    err |= ch_meas_init(dev, CH_DEFAULT_MEAS_NUM, &meas_cfg, NULL);

    /* GPT rangefinding algorithm + thresholds */
    err |= icu_gpt_algo_init(dev, &rx_algo_cfg);
    err |= icu_gpt_algo_configure(dev, CH_DEFAULT_MEAS_NUM, &rx_gpt_cfg, &rx_thresholds);

    /* Measurement queue: count (matching the sender's TX burst duration) -> receive, no
     * transmit segment - see AN-000175 ("Count Segments") and receiver_loop.c. */
    uint16_t tx_match_cycles = (uint16_t)ch_usec_to_cycles(dev, PC_TX_PULSE_US);
    err |= ch_meas_add_segment_count(dev, CH_DEFAULT_MEAS_NUM, tx_match_cycles, 0);
    err |= ch_meas_add_segment_rx(dev, CH_DEFAULT_MEAS_NUM, ICU_MAX_NUM_SAMPLES, RX_GAIN_REDUCE, RX_ATTEN,
                                  1 /* done interrupt on the last RX segment */);
    err |= ch_meas_write_config(dev);

    /* Push the algorithm configuration to the sensor */
    err |= ch_set_algo_config(dev, &rx_algo_cfg);
    err |= ch_init_algo(dev);

    /* High-level settings; ch_set_mode() must be last - it starts sensing (arms the sensor to
     * react to the next externally-driven INT1 trigger edge, which this board itself drives). */
    err |= ch_set_max_range(dev, PC_MAX_RANGE_MM);
    err |= ch_set_mode(dev, CH_MODE_TRIGGERED_RX_ONLY);

    return err ? -1 : 0;
}

/* ============================ Sender-ready GPIO ============================ *
 * Same tap as receiver_loop.c - see pitch_catch_common.h. */

static void sender_ready_gpio_init(void) {
    gpio_config_t ready_cfg = {
        .pin_bit_mask = 1ULL << PC_SENDER_READY_GPIO,
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,  /* defensive; primary pull-up is external, shared
                                              * with the sender's own INT2 net */
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE, /* polled immediately before each trigger, not
                                            * interrupt-driven */
    };
    gpio_config(&ready_cfg);
}

/* ============================ Raw I/Q dump ============================ */

static void print_iq_dump(uint32_t meas_num) {
    if (summary.iq_err != 0) {
        ESP_LOGW(TAG, "#%" PRIu32 "  ch_get_iq_data() failed, err=%u - no dump", meas_num, summary.iq_err);
        return;
    }

    if (summary.have_target) {
        printf("IQ_BEGIN meas=%" PRIu32 " num_samples=%u target=1 range_mm=%.1f amp=%u\n", meas_num,
               summary.num_samples, summary.range_q5 / 32.0f, summary.amplitude);
    } else {
        printf("IQ_BEGIN meas=%" PRIu32 " num_samples=%u target=0 range_mm=NA amp=0\n", meas_num,
               summary.num_samples);
    }
    for (uint16_t i = 0; i < summary.num_samples; i++) {
        printf("IQ,%u,%d,%d\n", i, iq_buf[i].i, iq_buf[i].q);
    }
    printf("IQ_END\n");
}

/* ============================ Public entry point ============================ */

void readout_run(ch_group_t *grp, ch_dev_t *dev) {
    data_ready_sem = xSemaphoreCreateBinary();
    if (data_ready_sem == NULL) {
        ESP_LOGE(TAG, "semaphore alloc failed - halting");
        for (;;) {
            vTaskDelay(portMAX_DELAY);
        }
    }

    ch_io_int_callback_set(grp, readout_data_ready_cb);

    ESP_LOGI(TAG, "configuring: triggered RX-only, max range %u mm", PC_MAX_RANGE_MM);
    if (readout_configure(dev) != 0) {
        ESP_LOGE(TAG, "sensor configuration failed - halting");
        for (;;) {
            vTaskDelay(portMAX_DELAY);
        }
    }
    ESP_LOGI(TAG, "configured: op freq %" PRIu32 " Hz, %u active samples, max range %u mm",
             ch_get_frequency(dev), ch_get_num_samples(dev), ch_get_max_range(dev));

    sender_ready_gpio_init();

    const TickType_t response_timeout = pdMS_TO_TICKS(PC_RESPONSE_TIMEOUT_MS);
    char             line[16];
    uint32_t         n = 0;

    ESP_LOGI(TAG, "ready - type Enter to trigger a measurement");

    for (;;) {
        if (fgets(line, sizeof(line), stdin) == NULL) {
            vTaskDelay(pdMS_TO_TICKS(100)); /* console not connected yet / no input - avoid a busy spin */
            continue;
        }

        if (gpio_get_level(PC_SENDER_READY_GPIO) == 0) {
            ESP_LOGW(TAG, "sender not ready (GPIO%d low, INT2 still asserted) - skipping trigger",
                     PC_SENDER_READY_GPIO);
            continue;
        }

        /* Pulses this board's INT1; the shared net also fires the sender's sensor. */
        ch_group_trigger(grp);
        n++;

        if (xSemaphoreTake(data_ready_sem, response_timeout) != pdTRUE) {
            ESP_LOGW(TAG, "#%" PRIu32 "  no response within %u ms - check INT1/INT2 wiring, sender power",
                     n, (unsigned)PC_RESPONSE_TIMEOUT_MS);
            continue;
        }

        print_iq_dump(n);
    }
}
