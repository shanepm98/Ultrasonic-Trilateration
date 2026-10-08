/*! \file receiver_loop.c
 *
 * \brief Implementation of the bring-up pitch-catch receiver. See receiver_loop.h.
 *
 * Flow: build a measurement queue (count -> receive, no transmit segment), configure the icu_gpt
 * rangefinding algorithm + detection thresholds, set 5 m full-scale range, switch to
 * CH_MODE_TRIGGERED_RX_ONLY, then own the periodic trigger loop: check the sender-ready GPIO,
 * call ch_group_trigger() (which pulses this board's INT1 - shared with the sender's board, so
 * it also fires the sender's sensor), and print each measured direct-path distance as it arrives.
 *
 * The receiver owns the trigger loop (rather than the sender, as in SonicLib's "typical"
 * example) so it knows its own trigger instant exactly, with no communication latency - see
 * ../README.md.
 */

#include "receiver_loop.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "driver/gpio.h"

#include <invn/soniclib/soniclib.h>
#include <invn/soniclib/sensor_fw/icu_gpt/icu_gpt.h> /* icu_gpt_algo_*, InvnAlgoRangeFinderConfig, ch_thresholds_t */

#include "pitch_catch_common.h" /* PC_TX_PULSE_US, PC_ODR, PC_MAX_RANGE_MM, PC_TRIGGER_INTERVAL_MS,
                                  * PC_RESPONSE_TIMEOUT_MS, PC_SENDER_READY_GPIO */

static const char *TAG = "pitch-catch-rx";

/* ============================ Application configuration ============================ *
 * PC_TX_PULSE_US / PC_ODR / PC_MAX_RANGE_MM / PC_TRIGGER_INTERVAL_MS / PC_RESPONSE_TIMEOUT_MS /
 * PC_SENDER_READY_GPIO come from pitch_catch_common.h - they must match the sender's
 * expectations. The gain/attenuation/threshold values below are local: independently tunable,
 * since this RX-only sensor's own noise floor/ringdown differs from the sender's. */

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
    .iq_output_format        = CH_OUTPUT_IQ,
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
} rx_sample_t;

static QueueHandle_t rx_queue; /* length 1, overwrite semantics - always the latest sample */

/* Called by SonicLib after each measurement. Runs in the BSP's bsp_int_task (task level), so
 * blocking SPI reads here are fine. */
static void rx_data_ready_cb(ch_group_t *grp, uint8_t io_index, ch_interrupt_type_t int_type) {
    if (int_type != CH_INTERRUPT_TYPE_DATA_RDY) {
        return; /* ignore program-loaded / error / other interrupt types */
    }

    ch_dev_t *dev   = ch_get_dev_ptr(grp, io_index);
    uint32_t  range = ch_get_range(dev, CH_RANGE_DIRECT); /* direct pitch-catch: separate objects */

    rx_sample_t s = {
        .have_target = (range != CH_NO_TARGET),
        .range_q5    = range,
        .amplitude   = (range != CH_NO_TARGET) ? ch_get_amplitude(dev) : 0,
    };
    xQueueOverwrite(rx_queue, &s);
}

/* ============================ Configuration ============================ */

static int receiver_configure(ch_dev_t *dev) {
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
     * transmit segment. Per AN-000175 ("Count Segments"): "a sensor in receive-only mode ...
     * will not have any transmit segments ... but it can instead use a count segment before the
     * receive segments, to match the timing of the transmitting sensor. In this case, the count
     * segment cycle count should match the other sensor's transmit cycle count." */
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
 * Separate from the receiver's own local sensor INT2 (GPIO4, handled by soniclib_esp32_bsp) -
 * this pin taps the sender board's INT2 line directly, over a second physical wire, so this
 * board can tell whether the sender's previous measurement has finished before retriggering. */

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

/* ============================ Public entry point ============================ */

void receiver_run(ch_group_t *grp, ch_dev_t *dev) {
    rx_queue = xQueueCreate(1, sizeof(rx_sample_t));
    if (rx_queue == NULL) {
        ESP_LOGE(TAG, "queue alloc failed - halting");
        for (;;) {
            vTaskDelay(portMAX_DELAY);
        }
    }

    ch_io_int_callback_set(grp, rx_data_ready_cb);

    ESP_LOGI(TAG, "configuring: triggered RX-only, max range %u mm", PC_MAX_RANGE_MM);
    if (receiver_configure(dev) != 0) {
        ESP_LOGE(TAG, "sensor configuration failed - halting");
        for (;;) {
            vTaskDelay(portMAX_DELAY);
        }
    }
    ESP_LOGI(TAG, "configured: op freq %" PRIu32 " Hz, %u active samples, max range %u mm",
             ch_get_frequency(dev), ch_get_num_samples(dev), ch_get_max_range(dev));

    sender_ready_gpio_init();

    ESP_LOGI(TAG, "running: triggering every %u ms (response timeout %u ms)",
             (unsigned)PC_TRIGGER_INTERVAL_MS, (unsigned)PC_RESPONSE_TIMEOUT_MS);

    const TickType_t response_timeout = pdMS_TO_TICKS(PC_RESPONSE_TIMEOUT_MS);
    rx_sample_t      s;
    uint32_t         n = 0;

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(PC_TRIGGER_INTERVAL_MS));

        if (gpio_get_level(PC_SENDER_READY_GPIO) == 0) {
            ESP_LOGW(TAG, "sender not ready (GPIO%d low, INT2 still asserted) - skipping trigger",
                     PC_SENDER_READY_GPIO);
            continue;
        }

        /* Pulses this board's INT1; the shared net also fires the sender's sensor. */
        ch_group_trigger(grp);
        n++;

        if (xQueueReceive(rx_queue, &s, response_timeout) != pdTRUE) {
            ESP_LOGW(TAG, "#%" PRIu32 "  no response within %u ms - check INT1/INT2 wiring, sender power",
                     n, (unsigned)PC_RESPONSE_TIMEOUT_MS);
            continue;
        }
        if (s.have_target) {
            ESP_LOGI(TAG, "#%" PRIu32 "  direct range %8.1f mm  (amp %u)", n, s.range_q5 / 32.0f, s.amplitude);
        } else {
            ESP_LOGI(TAG, "#%" PRIu32 "  no target", n);
        }
    }
}
