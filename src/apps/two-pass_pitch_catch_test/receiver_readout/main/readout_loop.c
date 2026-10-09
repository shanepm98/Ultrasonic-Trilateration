/*! \file readout_loop.c
 *
 * \brief Implementation of the two-pass pitch-catch receiver's raw I/Q readout. See readout_loop.h.
 *
 * Each reading is a pair of triggers, using the ICU's two measurement slots:
 *   - meas 0 (coarse): PC_ODR (f_op/4), count (TX match) -> receive, full ~5 m window. Identical to
 *     pitch_catch_mode_hardware_test's receiver_readout.
 *   - meas 1 (fine): TP_FINE_ODR (f_op/2, ~2.5 m window), count (TX match + pad) -> receive. Before
 *     each fine pass the pad is set from the coarse distance (one of three bands - see ../../README.md)
 *     and the detection thresholds become a gate around the expected arrival.
 * The sender is unchanged: the receiver fires it over the shared INT1 line for both passes, and it
 * sends the same chirp each time. Only the fine pass's I/Q is read out; the coarse pass reports just
 * the sensor's distance. Nothing is printed until both passes are done, so the serial dump doesn't
 * stretch the time between the two chirps.
 *
 * The console prompts for a batch title, the actual (tape-measured) sender-receiver distance and a
 * reading count, waits for Enter, then runs that many pairs and prints each pass as a BEGIN/END block
 * (coarse: distance summary only; fine: summary + raw I/Q).
 *
 * Pad compensation: for an RX-only measurement SonicLib counts every COUNT segment before the
 * receive segment as pre-RX time (ch_common_meas_update_counts()), and icu_gpt adds it back into
 * the time of flight (get_tof_offset_lsb()), using the slot/ODR the sensor reports for the
 * measurement that just finished. So the fine pass's range_mm should already be the absolute
 * distance, not the distance from the start of the padded window. To be confirmed on hardware.
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

#include "console_io.h"
#include "sensor_calibration.h" /* sc_rx_thresholds, SC_RX_THRESHOLDS_RINGDOWN_SAMPLES */
#include "sensor_offsets.h"     /* TRANSMITTER_OFFSET, RECEIVER_OFFSET */
#include "pitch_catch_common.h" /* PC_TX_PULSE_US, PC_ODR, PC_MAX_RANGE_MM, PC_RESPONSE_TIMEOUT_MS,
                                  * PC_SENDER_READY_GPIO */

static const char *TAG = "two-pass-rx-readout";

/* ============================ Application configuration ============================ *
 * The coarse pass's detection thresholds are the bench-calibrated sc_rx_thresholds from the
 * sensor_calibration component (fitted at PC_ODR / PC_TX_PULSE_US - the coarse pass's settings).
 * Its ringdown cancel must match the one used during that capture. */

#define RX_GAIN_REDUCE 0u
#define RX_ATTEN       0u

#define RX_RINGDOWN_CANCEL_SAMPLES SC_RX_THRESHOLDS_RINGDOWN_SAMPLES
#define RX_STATIC_FILTER_SAMPLES   0u
#define RX_NUM_RANGES              1u

static const icu_gpt_algo_config_t rx_gpt_cfg = {
    .ringdown_cancel_samples = RX_RINGDOWN_CANCEL_SAMPLES,
    .static_filter_samples   = RX_STATIC_FILTER_SAMPLES,
    .iq_output_format        = CH_OUTPUT_IQ, /* required for ch_get_iq_data() to return valid data */
    .num_ranges              = RX_NUM_RANGES,
    .filter_update_interval  = 0,
};

/* ============================ Two-pass configuration ============================ */

#define TP_COARSE_MEAS 0u /* CH_DEFAULT_MEAS_NUM: the slot ch_set_max_range() etc. act on */
#define TP_FINE_MEAS   1u
#define TP_FINE_ODR    CH_ODR_FREQ_DIV_2 /* max ODR, ~2.5 m window at ICU_MAX_NUM_SAMPLES */

/* Bands, chosen from the coarse distance d (see ../../README.md):
 *   band 1: d <= TP_BAND1_MAX_MM           no pad,     window ~0    - 2.5  m
 *   band 2: TP_BAND1_MAX_MM < d < BAND3    TP_PAD2_MM, window ~1.25 - 3.75 m
 *   band 3: d >= TP_BAND3_MIN_MM           TP_PAD3_MM, window ~2.5  - 5    m */
#define TP_BAND1_MAX_MM 2000u
#define TP_BAND3_MIN_MM 3000u
#define TP_PAD2_MM      1250u
#define TP_PAD3_MM      2500u

/* Fine-pass detection gate: TP_GATE_LEVEL within +/- TP_GATE_HALF_MM of the coarse distance,
 * TP_GATE_BLOCK_LEVEL (unreachable) everywhere else. TP_GATE_LEVEL is a PLACEHOLDER - no
 * captures exist at f_op/2 yet, so it is not calibrated. The I/Q dump is the real output of this
 * app; tune this from it. */
#define TP_GATE_HALF_MM     200u
#define TP_GATE_LEVEL       400u
#define TP_GATE_BLOCK_LEVEL UINT16_MAX

/* Pause between the coarse and fine triggers, so the coarse chirp's reverberation dies out. */
#define TP_PASS_GAP_MS 20u

static const icu_gpt_algo_config_t fine_gpt_cfg = {
    .ringdown_cancel_samples = 0, /* no TX on this sensor, and the gate blocks the window start */
    .static_filter_samples   = RX_STATIC_FILTER_SAMPLES,
    .iq_output_format        = CH_OUTPUT_IQ,
    .num_ranges              = RX_NUM_RANGES,
    .filter_update_interval  = 0,
};

/* Raw per-measurement algorithm config the sensor receives; filled by icu_gpt_algo_configure(). */
static InvnAlgoRangeFinderConfig rx_algo_cfg;

/* Leading count segment length for both slots (matches the sender's TX burst). Set by
 * readout_configure(). */
static uint16_t tx_match_cycles;

/* ============================ Data-ready plumbing ============================ */

typedef struct {
    bool     have_target;
    uint32_t range_q5; /* ch_get_range() units: millimetres * 32 */
    uint16_t amplitude;
    uint16_t num_samples;
    uint8_t  iq_err; /* ch_get_iq_data() return code; 0 = OK */
} readout_summary_t;

/* One summary per measurement slot, indexed by ch_meas_get_last_num(). Only one measurement is
 * ever in flight at a time (on-demand triggering), and a pair's two passes land in different
 * slots, so both survive until the pair is printed. Only the fine pass's I/Q is read out. */
static ch_iq_sample_t     iq_buf[ICU_MAX_NUM_SAMPLES];
static readout_summary_t  summary[2];
static uint8_t            last_slot; /* slot of the most recent data-ready */
static SemaphoreHandle_t  data_ready_sem;

/* Called by SonicLib after each measurement. Runs in the BSP's bsp_int_task (task level), so
 * blocking SPI reads here - including the I/Q readout - are fine. */
static void readout_data_ready_cb(ch_group_t *grp, uint8_t io_index, ch_interrupt_type_t int_type) {
    if (int_type != CH_INTERRUPT_TYPE_DATA_RDY) {
        return; /* ignore program-loaded / error / other interrupt types */
    }

    ch_dev_t *dev   = ch_get_dev_ptr(grp, io_index);
    uint8_t   slot  = ch_meas_get_last_num(dev) ? TP_FINE_MEAS : TP_COARSE_MEAS;
    uint32_t  range = ch_get_range(dev, CH_RANGE_DIRECT); /* direct pitch-catch: separate objects */

    readout_summary_t *sum = &summary[slot];
    sum->have_target = (range != CH_NO_TARGET);
    sum->range_q5    = range;
    sum->amplitude   = sum->have_target ? ch_get_amplitude(dev) : 0;
    /* Per-slot count: ch_get_num_samples() always reports the default (coarse) measurement. */
    sum->num_samples = ch_meas_get_num_samples(dev, slot);
    if (sum->num_samples > ICU_MAX_NUM_SAMPLES) {
        sum->num_samples = ICU_MAX_NUM_SAMPLES; /* defensive */
    }

    /* Coarse pass: distance only - skipping its I/Q read also keeps the gap to the fine pass short. */
    sum->iq_err = (slot == TP_FINE_MEAS) ? ch_get_iq_data(dev, iq_buf, 0, sum->num_samples, CH_IO_MODE_BLOCK) : 0;

    last_slot = slot;
    xSemaphoreGive(data_ready_sem);
}

/* ============================ Configuration ============================ *
 * The coarse slot is identical to pitch_catch_mode_hardware_test's receiver_configure() - see
 * that app's README for the segment-timing rationale. */

static int readout_configure(ch_dev_t *dev) {
    uint8_t err = 0;

    ch_meas_config_t coarse_cfg = {
        .odr         = PC_ODR,
        .meas_period = 0, /* not used in triggered mode */
        .mode        = CH_MEAS_MODE_ACTIVE,
    };
    err |= ch_meas_init(dev, TP_COARSE_MEAS, &coarse_cfg, NULL);

    /* GPT rangefinding algorithm + thresholds */
    err |= icu_gpt_algo_init(dev, &rx_algo_cfg);
    err |= icu_gpt_algo_configure(dev, TP_COARSE_MEAS, &rx_gpt_cfg, &sc_rx_thresholds);

    /* Coarse queue: count (matching the sender's TX burst duration) -> receive, no transmit
     * segment - see AN-000175 ("Count Segments"). */
    tx_match_cycles = (uint16_t)ch_usec_to_cycles(dev, PC_TX_PULSE_US);
    err |= ch_meas_add_segment_count(dev, TP_COARSE_MEAS, tx_match_cycles, 0);
    err |= ch_meas_add_segment_rx(dev, TP_COARSE_MEAS, ICU_MAX_NUM_SAMPLES, RX_GAIN_REDUCE, RX_ATTEN,
                                  1 /* done interrupt on the last RX segment */);

    /* Fine queue, starting in standby: one count segment (TX match + pad, rewritten by
     * fine_prepare() before each fine pass) -> receive at max ODR. Band 1 (no pad) until then,
     * with a fully open gate. */
    ch_meas_config_t fine_cfg = {
        .odr         = TP_FINE_ODR,
        .meas_period = 0,
        .mode        = CH_MEAS_MODE_STANDBY,
    };
    const ch_thresholds_t open_gate = {
        .threshold = {{.start_sample = 0, .level = TP_GATE_LEVEL}},
    };
    err |= ch_meas_init(dev, TP_FINE_MEAS, &fine_cfg, NULL);
    err |= icu_gpt_algo_configure(dev, TP_FINE_MEAS, &fine_gpt_cfg, &open_gate);
    err |= ch_meas_add_segment_count(dev, TP_FINE_MEAS, tx_match_cycles, 0);
    err |= ch_meas_add_segment_rx(dev, TP_FINE_MEAS, ICU_MAX_NUM_SAMPLES, RX_GAIN_REDUCE, RX_ATTEN, 1);

    err |= ch_meas_write_config(dev);

    /* Push the algorithm configuration to the sensor */
    err |= ch_set_algo_config(dev, &rx_algo_cfg);
    err |= ch_init_algo(dev);

    /* High-level settings; ch_set_mode() must be last - it starts sensing (arms the sensor to
     * react to the next externally-driven INT1 trigger edge, which this board itself drives).
     * ch_set_max_range() only touches the default (coarse) slot. */
    err |= ch_set_max_range(dev, PC_MAX_RANGE_MM);
    err |= ch_set_mode(dev, CH_MODE_TRIGGERED_RX_ONLY);

    return err ? -1 : 0;
}

/* ============================ Fine-pass setup ============================ */

typedef struct {
    uint8_t  band;      /* 1..3 */
    uint16_t pad_mm;
    uint32_t coarse_mm;
} fine_plan_t;

static uint16_t clamp_sample(int32_t s) {
    if (s < 0) {
        return 0;
    }
    return (s > (int32_t)ICU_MAX_NUM_SAMPLES) ? ICU_MAX_NUM_SAMPLES : (uint16_t)s;
}

/* Choose the band for coarse_mm, rewrite the fine slot's pad and detection gate, and make the fine
 * slot the only active one (writes the queue to the sensor). Requires readout_configure(). Returns
 * 0 on success. Always pair with fine_restore(). */
static int fine_prepare(ch_dev_t *dev, uint32_t coarse_mm, fine_plan_t *plan) {
    uint8_t err = 0;

    plan->coarse_mm = coarse_mm;
    if (coarse_mm <= TP_BAND1_MAX_MM) {
        plan->band = 1, plan->pad_mm = 0;
    } else if (coarse_mm < TP_BAND3_MIN_MM) {
        plan->band = 2, plan->pad_mm = TP_PAD2_MM;
    } else {
        plan->band = 3, plan->pad_mm = TP_PAD3_MM;
    }

    /* Pad: lengthen the leading count segment in place. (ch_meas_insert_segment() at index 0
     * reads trx_inst[-1], so the instruction is edited directly instead.) ch_meas_standby() below
     * writes the queue, which also recomputes the slot's pre-RX cycle count used for ToF
     * compensation. */
    uint32_t pad_us       = (uint32_t)plan->pad_mm * 1000u / CH_SPEEDOFSOUND_MPS;
    uint32_t count_cycles = tx_match_cycles + ch_usec_to_cycles(dev, pad_us);
    if (count_cycles > UINT16_MAX) {
        ESP_LOGE(TAG, "pad %u mm -> %" PRIu32 " count cycles, over the segment limit", plan->pad_mm,
                 count_cycles);
        return -1;
    }
    dev->meas_queue.meas[TP_FINE_MEAS].trx_inst[0].length = (uint16_t)count_cycles;

    /* Gate around the expected arrival, in fine-window samples. Thresholds hold from their
     * start_sample to the next entry's, so the trailing entries just keep the block level. */
    int32_t  rel_mm     = (int32_t)coarse_mm - (int32_t)plan->pad_mm;
    int32_t  lo_mm      = rel_mm - (int32_t)TP_GATE_HALF_MM;
    int32_t  hi_mm      = rel_mm + (int32_t)TP_GATE_HALF_MM;
    uint16_t gate_start = clamp_sample(lo_mm <= 0 ? 0 : ch_meas_mm_to_samples(dev, TP_FINE_MEAS, (uint16_t)lo_mm));
    uint16_t gate_end   = clamp_sample(hi_mm <= 0 ? 0 : ch_meas_mm_to_samples(dev, TP_FINE_MEAS, (uint16_t)hi_mm));

    ch_thresholds_t gate = {
        .threshold = {
            {.start_sample = 0, .level = TP_GATE_BLOCK_LEVEL},
            {.start_sample = gate_start, .level = TP_GATE_LEVEL},
        },
    };
    for (uint8_t i = 2; i < CH_NUM_THRESHOLDS; i++) {
        gate.threshold[i].start_sample = gate_end;
        gate.threshold[i].level        = TP_GATE_BLOCK_LEVEL;
    }
    err |= icu_gpt_set_thresholds(dev, TP_FINE_MEAS, &gate);

    ch_meas_standby(dev, TP_COARSE_MEAS); /* fine slot becomes the only active one */

    return err ? -1 : 0;
}

/* Make the coarse slot the only active one again. */
static void fine_restore(ch_dev_t *dev) {
    ch_meas_standby(dev, TP_FINE_MEAS);
}

/* Read the measurement queue back from the sensor's memory (not SonicLib's host-side copy, which
 * is what ch_meas_get_odr() returns) and log the ODR the sensor will actually use for each slot.
 * Requires readout_configure(). chdrv_meas_queue_read() is a SonicLib-internal driver call. */
static void print_sensor_odr(ch_dev_t *dev) {
    static measurement_queue_t sensor_queue; /* separate buffer - leaves dev->meas_queue untouched */
    static const ch_odr_t      expected[2] = {PC_ODR, TP_FINE_ODR};

    if (chdrv_meas_queue_read(dev, &sensor_queue) != 0) {
        ESP_LOGW(TAG, "measurement queue readback failed - ODR unknown");
        return;
    }

    for (uint8_t slot = 0; slot < 2; slot++) {
        uint8_t odr = sensor_queue.meas[slot].odr;
        if (odr < CH_ODR_FREQ_DIV_32 || odr > CH_ODR_FREQ_DIV_2) {
            ESP_LOGW(TAG, "meas %u sensor ODR: %u - not a valid ch_odr_t value", slot, odr);
            continue;
        }
        uint32_t divisor = 1u << (7 - odr); /* CH_ODR_FREQ_DIV_8 = 4 -> 2^(7-4) = 8 (ch_odr_t) */
        ESP_LOGI(TAG, "meas %u sensor ODR: %u (op freq / %" PRIu32 " = %" PRIu32 " samples/s), %u samples",
                 slot, odr, divisor, ch_get_frequency(dev) / divisor, ch_meas_get_num_samples(dev, slot));
        if (odr != expected[slot]) {
            ESP_LOGW(TAG, "meas %u sensor ODR %u != expected %u", slot, odr, (unsigned)expected[slot]);
        }
    }
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

/* ============================ Batch readout ============================ */

#define BATCH_TITLE_MAX    64u   /* incl. NUL */
#define BATCH_MAX_READINGS 1000u
#define SENDER_READY_TIMEOUT_MS PC_TRIGGER_INTERVAL_MS

typedef struct {
    char     title[BATCH_TITLE_MAX];
    uint32_t actual_mm;
    uint32_t count;
} batch_t;

/* Wait for the sender's INT2 (GPIO33) to read high, i.e. its last measurement has been serviced.
 * Requires sender_ready_gpio_init(). Returns false on timeout. */
static bool wait_sender_ready(void) {
    TickType_t start = xTaskGetTickCount();
    while (gpio_get_level(PC_SENDER_READY_GPIO) == 0) {
        if (xTaskGetTickCount() - start >= pdMS_TO_TICKS(SENDER_READY_TIMEOUT_MS)) {
            return false;
        }
        vTaskDelay(1);
    }
    return true;
}

/* Every pass prints exactly one BEGIN/END block, so a batch always holds 2 * batch->count blocks
 * (coarse then fine): either the meas= summary (+ IQ rows for the fine pass), or a single error=
 * line. The pass= line
 * identifies the pass; extract_measurements.py records it in the manifest. */
static void print_block_header(const batch_t *batch, const char *pass_line) {
    printf("BEGIN\n");
    printf("title=%s\n", batch->title);
    printf("actual_mm=%" PRIu32 "\n", batch->actual_mm);
    printf("%s\n", pass_line);
}

static void print_error_block(const batch_t *batch, const char *pass_line, uint32_t meas_num, const char *err) {
    print_block_header(batch, pass_line);
    printf("meas=%" PRIu32 " error=%s\n", meas_num, err);
    printf("END\n");
}

/* Summary line for the pass in slot; the fine pass also gets its IQ rows. */
static void print_pass_block(const batch_t *batch, const char *pass_line, uint32_t meas_num, uint8_t slot) {
    const readout_summary_t *sum = &summary[slot];

    if (sum->iq_err != 0) {
        char err[24];
        snprintf(err, sizeof(err), "iq_read_failed_%u", sum->iq_err);
        print_error_block(batch, pass_line, meas_num, err);
        return;
    }

    print_block_header(batch, pass_line);
    if (sum->have_target) {
        /* range_mm is corrected to the base of each unit, where actual_mm is measured from. */
        float range_mm = sum->range_q5 / 32.0f + TRANSMITTER_OFFSET + RECEIVER_OFFSET;
        printf("meas=%" PRIu32 " num_samples=%u target=1 range_mm=%.1f amp=%u\n", meas_num,
               sum->num_samples, range_mm, sum->amplitude);
    } else {
        printf("meas=%" PRIu32 " num_samples=%u target=0 range_mm=NA amp=0\n", meas_num, sum->num_samples);
    }
    for (uint16_t i = 0; slot == TP_FINE_MEAS && i < sum->num_samples; i++) {
        printf("IQ,%u,%d,%d\n", i, iq_buf[i].i, iq_buf[i].q);
    }
    printf("END\n");
}

static void prompt_batch(batch_t *batch) {
    console_read_line("Batch title: ", batch->title, sizeof(batch->title));
    batch->actual_mm = console_read_uint("Actual distance (mm): ", 1, PC_MAX_RANGE_MM);
    batch->count     = console_read_uint("Number of readings: ", 1, BATCH_MAX_READINGS);

    printf("Press ENTER to start %" PRIu32 " readings...", batch->count);
    fflush(stdout);
    while (!console_is_enter(console_getc())) {
    }
    printf("\n\n");
}

/* Wait for the sender, fire one trigger and wait for this sensor's data-ready. Returns NULL on
 * success, else an error tag for the block. On success *slot is the slot that ran. */
static const char *trigger_and_wait(ch_group_t *grp, uint8_t *slot) {
    if (!wait_sender_ready()) {
        return "sender_not_ready";
    }

    xSemaphoreTake(data_ready_sem, 0); /* drop any stale give from a late previous response */

    /* Pulses this board's INT1; the shared net also fires the sender's sensor. */
    ch_group_trigger(grp);

    if (xSemaphoreTake(data_ready_sem, pdMS_TO_TICKS(PC_RESPONSE_TIMEOUT_MS)) != pdTRUE) {
        return "no_response";
    }
    *slot = last_slot;
    return NULL;
}

/* One reading = coarse trigger + fine trigger -> two BEGIN/END blocks, printed after both passes.
 * meas_num counts readings within the batch, from 1. */
static void run_one_reading(ch_group_t *grp, ch_dev_t *dev, const batch_t *batch, uint32_t meas_num) {
    char        coarse_line[24];
    char        fine_line[96];
    uint8_t     slot;
    fine_plan_t plan;

    snprintf(coarse_line, sizeof(coarse_line), "pass=coarse odr=%u", (unsigned)PC_ODR);
    snprintf(fine_line, sizeof(fine_line), "pass=fine odr=%u", (unsigned)TP_FINE_ODR); /* until planned */

    /* Coarse pass */
    const char *err = trigger_and_wait(grp, &slot);
    if (err == NULL && slot != TP_COARSE_MEAS) {
        err = "wrong_slot";
    }
    if (err != NULL) {
        print_error_block(batch, coarse_line, meas_num, err);
        print_error_block(batch, fine_line, meas_num, "coarse_failed");
        fine_restore(dev); /* resync in case the sensor's slot order drifted */
        return;
    }
    if (!summary[TP_COARSE_MEAS].have_target) {
        print_pass_block(batch, coarse_line, meas_num, TP_COARSE_MEAS);
        print_error_block(batch, fine_line, meas_num, "no_coarse_target");
        return;
    }

    /* Fine pass. Band and gate use the sensor's own (uncorrected) range: the fine window and its
     * samples are in the sensor's time base, so the mounting offsets don't belong here. */
    uint32_t coarse_mm = summary[TP_COARSE_MEAS].range_q5 / 32u;
    if (fine_prepare(dev, coarse_mm, &plan) != 0) {
        err = "fine_config_failed";
    } else {
        vTaskDelay(pdMS_TO_TICKS(TP_PASS_GAP_MS));
        err = trigger_and_wait(grp, &slot);
        if (err == NULL && slot != TP_FINE_MEAS) {
            err = "wrong_slot";
        }
    }
    fine_restore(dev);

    /* coarse_mm: the coarse pass's range as reported by the sensor (no mounting offsets), at full
     * resolution - the band/gate math above uses it truncated to whole mm. */
    snprintf(fine_line, sizeof(fine_line), "pass=fine odr=%u band=%u pad_mm=%u coarse_mm=%.1f",
             (unsigned)TP_FINE_ODR, plan.band, plan.pad_mm, summary[TP_COARSE_MEAS].range_q5 / 32.0f);

    print_pass_block(batch, coarse_line, meas_num, TP_COARSE_MEAS);
    if (err != NULL) {
        print_error_block(batch, fine_line, meas_num, err);
    } else {
        print_pass_block(batch, fine_line, meas_num, TP_FINE_MEAS);
    }
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

    ESP_LOGI(TAG, "configuring: triggered RX-only, two-pass (coarse ODR %u, fine ODR %u)", (unsigned)PC_ODR,
             (unsigned)TP_FINE_ODR);
    if (readout_configure(dev) != 0) {
        ESP_LOGE(TAG, "sensor configuration failed - halting");
        for (;;) {
            vTaskDelay(portMAX_DELAY);
        }
    }
    ESP_LOGI(TAG, "configured: op freq %" PRIu32 " Hz, %u active samples, max range %u mm",
             ch_get_frequency(dev), ch_get_num_samples(dev), ch_get_max_range(dev));
    print_sensor_odr(dev);

    sender_ready_gpio_init();
    console_init();

    ESP_LOGI(TAG, "ready - sender-ready on GPIO%d", PC_SENDER_READY_GPIO);

    batch_t batch;
    for (;;) {
        printf("\n");
        prompt_batch(&batch);
        for (uint32_t n = 1; n <= batch.count; n++) {
            run_one_reading(grp, dev, &batch, n);
        }
        fflush(stdout);
    }
}
