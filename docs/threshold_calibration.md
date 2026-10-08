# Receiver detection threshold calibration

The pitch-catch receiver's `icu_gpt` algorithm reports the first sample whose magnitude crosses a
piecewise-constant threshold: 8 `{start_sample, level}` segments (`ch_thresholds_t`,
`CH_NUM_THRESHOLDS` = 8). This page describes how the table in
`src/components/sensor_calibration/rx_thresholds.c` (`sc_rx_thresholds`) was derived from bench
recordings.

## Data

The current table comes from `src/apps/pitch_catch_mode_hardware_test/readouts/parsed/`. These are
the 2026-10-07 hardwired pitch-catch captures; they are local only (gitignored) and were extracted
with `extract_measurements.py --odr 5`. There are 700 raw I/Q readings in 14 batches of 50:

| Distance | Batches |
|---|---|
| 0.5 m | straight |
| 1.5 m | straight, 30° TX, 45° TX, 45° RX |
| 3 m | straight, 30° TX, 45° RX |
| 4 m | straight, 30° TX, 30° RX |
| 5 m (4990 mm) | straight, 30° TX, 30° RX |

"TX" and "RX" name which board was turned. The data set is incomplete: the plan is to cover both
boards turned up to 45° at every distance.

Capture settings, from `pitch_catch_common.h`:
- **TX burst:** 450 µs at pulse width 4. That is about 640 sensor clock counts, which AN-000175
  Table 5 gives as the minimum to fully excite the transducer.
- **ODR:** f_op/4 (ODR 5), with op freq 88521 Hz.
- **Ringdown cancel:** 20 samples. Each reading has 339 samples.

An earlier table (2026-10-05) was fitted to 80 µs bursts at f_op/8. It is superseded, because both
the sample spacing and the signal strength changed.

**Distance to sample.** In pitch-catch the sound crosses the gap once:
`mm/sample = 343000 · 2^(7−ODR) / op_freq`, which is **15.5 mm/sample** here.
- **Pulse shape.** The received pulse ramps up for about the burst length (10 samples, 155 mm) and
  peaks at about the predicted index (+0.4 samples).
- **Arrival positions.** The script finds each batch's arrival from its median waveform. Every batch
  is within ±2 samples of its predicted index, except **5 m, which arrives 6 samples (~93 mm)
  early**. Either that distance was really about 4.9 m, or something else is going on there. The
  table uses the measured arrival positions, so it isn't thrown off either way.

## Method

The method is implemented in
`src/apps/pitch_catch_mode_hardware_test/receiver_readout/host/calibrate_thresholds.py`, which uses the
standard library only.

1. **Peaks and noise.**
   - Direct-path peak: the largest |I,Q| within ±4 samples of the batch's arrival.
   - Noise floor: every sample from the end of the ringdown up to the start of the pulse's ramp. This
     is where a noise crossing would become a false first target. The noise has a median of 14,
     p99.9 of 43 and a maximum of 59.
   - Later reflections don't matter here. With `num_ranges = 1` only the first crossing is reported,
     and the direct path is first.
2. **45° gate.** A batch with `45deg` in its title is fitted only if its SNR is at least
   `--min-snr` (default 2). SNR here means the weakest-1% peak divided by the noise maximum.
   - In the 2026-10-05 data, 4 m at 45° (SNR 1.3) was excluded this way.
   - In this data set every 45° batch passes: 1.5 m at 22–23, 3 m RX at 9.1.
   - `--include-45` fits every 45° batch regardless of SNR.
3. **Threshold units.** The sensor compares its own CORDIC magnitude against the threshold
   (`structs.h`: "Cordic_mag(I,Q) must exceed this level"), not hypot(I,Q) of the read-out data.
   - The scale `k` (|I,Q| per threshold unit) is fitted from the batches where the recording table
     caught some readings but not all. In a batch where the firmware detected a fraction f, the
     effective threshold is the (1−f) quantile of its peaks.
   - The fits were 1.44 (3 m 45° RX, 8/50 hits) and 1.50 (4 m straight, 2/50 hits). The 2026-10-05
     data gave 1.45–1.70.
   - The largest value, **k = 1.50**, is used, because it gives the lower and safer levels.
4. **Worst-case envelope.** For each distance, take the 1st-percentile peak of the weakest batch,
   placed at its measured arrival:

   | Distance | Weakest batch | Peak (p1) |
   |---|---|---|
   | 0.5 m | straight | 11837 |
   | 1.5 m | 45° RX | 1295 |
   | 3 m | 45° RX | 540 |
   | 4 m | 30° RX | 223 |
   | 4.9 m | 30° RX | 188 |

   Interpolate log-linearly between points, and extrapolate beyond the ends.
5. **Segments and levels.**
   - Segment 0 covers the ringdown window. Its level is high because those samples are cancelled.
   - Segments 1–7 split the range from the end of the ringdown (sample 20) to the point where the
     level reaches the noise floor. Each segment spans an equal drop in the envelope.
   - Level = 0.5 (−6 dB) × the envelope at the segment's far end / k.
   - Floor = 2 × noise max / k, which is 118 |I,Q|. Levels never rise with distance.
   - The last segment holds the floor out to 5 m and beyond.
6. **Replay.** Both tables are run on every reading: first index at or after the ringdown with
   |I,Q|/k ≥ level. A detection is counted as correct anywhere from the start of the ramp
   (peak − 14) to peak + 4. Any other detection is counted as false.

## Result

| start_sample | from (mm) | level | ≈ \|I,Q\| |
|---|---|---|---|
| 0 | — (ringdown) | 4075 | 6099 |
| 20 | 303 | 3326 | 4978 |
| 39 | 598 | 1785 | 2671 |
| 57 | 877 | 958 | 1434 |
| 75 | 1156 | 514 | 769 |
| 93 | 1435 | 275 | 412 |
| 148 | 2287 | 147 | 220 |
| 209 | 3232 | 78 | 117 |

The near segments are packed closely because the signal falls about 9× between 0.5 m and 1.5 m.

**Replay.** The new table detects every reading (50/50) in all 14 batches, with 0 false
detections. That includes 5 m at 30° TX and 30° RX. The recording table (the old 5-segment
placeholder) caught nothing at 4–5 m except 2/50 at 4 m straight.

**Effect of the longer burst** (450 µs, PW 4, compared with 80 µs, PW 3, on 2026-10-05). These are
straight-on median peaks. Not every gain comes from the burst: some come from the different ODR and
receive filter, which also scale the I/Q values.

| Distance | 2026-10-05 | 2026-10-07 |
|---|---|---|
| 3 m | 708 | 1162 |
| 4 m | 299 | 449 |

At 5 m the weakest batch's SNR is 3.2. With the old burst, off-axis 5 m was extrapolated to an SNR
of about 1.

**Caveats:**
- The 5 m arrival is 93 mm early; re-check that distance.
- Detection fires on the pulse's rising edge, up to 10 samples (155 mm) before the peak. So
  `range_mm` depends on signal strength as well as distance. This is a ranging-accuracy issue, not a
  detection issue.
- The table is untested on hardware until an app adopts it.

## Re-running and using it

```sh
cd src/apps/pitch_catch_mode_hardware_test
./receiver_readout/host/calibrate_thresholds.py --dry-run          # report only
./receiver_readout/host/calibrate_thresholds.py                    # rewrite rx_thresholds.c
./receiver_readout/host/calibrate_thresholds.py path/to/parsed --tx-us 450 --margin 0.5 --noise-mult 2
```

Options that must match the capture conditions, or the fit will be wrong:
- `--old-threshold START:LEVEL` (repeatable): the table the recordings were captured with. If it is
  wrong, `k` will be wrong.
- `--tx-us`: the sender's `PC_TX_PULSE_US`.
- `--ringdown-samples`: the receiver's ringdown cancel.

To adopt the table in an app:
1. Add `sensor_calibration` to the app's `main/CMakeLists.txt` `REQUIRES`.
2. `#include "sensor_calibration.h"`.
3. Pass `&sc_rx_thresholds` to `icu_gpt_algo_configure()` in place of the local `rx_thresholds`.

The table assumes the capture conditions recorded as `SC_RX_THRESHOLDS_*` in the header: ODR 5,
450 µs TX burst and 20-sample ringdown cancel. If any of them changes, regenerate it.
