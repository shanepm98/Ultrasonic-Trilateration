# Two-Pass Pitch-Catch Mode for Finer Resolution
The idea here is to build on the existing pitch-catch application to experiment with a method for taking distance measurements at highest-ODR at 5 meter distance.
5 meter readings are normally unobtainable at max ODR, as the 340 sample window runs out slightly after 2.5 meters. But because higher ODR means higher precision,
it is desirable to try and work around this problem.

In this app, I am testing the approach of refining measurements using two chirps. The first chirp will be sampled at a lower ODR, such as f_op/4. This first
chirp will be used to get an approximate position of the sensor, that does not have to be accurate beyond a few centimeters. 
Once the approximate distance from the sender is known, the receiver will dynamically construct a new sensor configuration that samples at the maximum ODR,
F_op/2. If the target is beyond 2.5m, it will delay the receive segment with count cycles to shift the 2.5m window within the 5m range. The sender will
emit a second chirp, this time sampled at the max ODR after an appropriate delay, in order to be able to reach the entire 5m range window.

For simplicity, We only need 3 possible configurations at max ODR to be able to sample any 2.5m window in the 5m range at max ODR. Let d be the distance
from sender as approximated using the first chirp. Then our 3 configurations are:
(1) 0m <= d <= 2m: no padding needed, simply record at full ODR
(2) 2m <  d  < 3m: Pad with 1.25m worth of count cycles, then record at full ODR
(3) 3m <= d <= 5m: Pad with 2.5m worth of count cycles, then record at full ODR

It may also be necessary to account for the count cycles, if the sensor does not do this already. This will need to be determined experimentally.

## Implementation (`receiver_readout/`)
The sender is unchanged from `pitch_catch_mode_hardware_test`. It sends the same chirp every time
the receiver pulls the shared INT1 line. Only `receiver_readout/` implements the two-pass flow, as a
data-collection tool. It prints the coarse pass's distance and the fine pass's raw I/Q, so the fine
pass can be tuned offline.

The ICU sensor holds two measurement definitions, and the receiver uses one per pass:

| Slot | Pass | ODR | Queue |
|---|---|---|---|
| meas 0 | coarse | `PC_ODR` (f_op/4) | count (TX match) -> rx, ~5 m; `sc_rx_thresholds` |
| meas 1 | fine | f_op/2 | count (TX match + pad) -> rx, ~2.5 m |

The coarse pass detects with the bench-calibrated `sc_rx_thresholds` from the `sensor_calibration`
component, with its ringdown cancel tied to `SC_RX_THRESHOLDS_RINGDOWN_SAMPLES`. That table was
fitted at the coarse pass's ODR and TX burst.

For each reading the receiver:
1. Triggers the coarse pass. If it finds no target, no fine pass is run.
2. Picks the band from the coarse range (the table above). It then lengthens the fine slot's count
   segment by the pad, and sets the fine slot's thresholds to a gate around the expected arrival.
3. Makes the fine slot active (`ch_meas_standby(dev, 0)`) and waits `TP_PASS_GAP_MS`.
4. Triggers the fine pass, then switches back to the coarse slot.
5. Prints both blocks. Only the fine pass's I/Q is read from the sensor. Nothing is printed until
   both passes are done, so serial output doesn't stretch the time between the chirps.

**Count-cycle compensation (open question above).** SonicLib already handles this in software. For
an RX-only measurement, it counts every count segment before the receive segment as pre-RX time
(`ch_common_meas_update_counts()`). `icu_gpt` adds that back into the time of flight
(`get_tof_offset_lsb()`). So the fine pass's `range_mm` should already be the absolute distance.
This is **not yet confirmed on hardware**. If fine `range_mm` comes out about `pad_mm` short of the
coarse value, the compensation isn't happening.

**Fine-pass detection is a placeholder.** No captures exist at f_op/2 yet. The gate is
`TP_GATE_LEVEL` within ±`TP_GATE_HALF_MM` of the coarse distance, and unreachable everywhere else.
The level is a guess, so treat the fine pass's `target`/`range_mm` as provisional until it has been
tuned from the I/Q captured here. All `TP_*` values are at the top of `readout_loop.c`.

## Usage
Flash `sender/` and `receiver_readout/` (`./build.sh flash monitor` in each). The startup log
prints both slots' ODR as read back from the sensor; check that meas 0 is 5 and meas 1 is 6. The
console prompts for a batch the same way as `pitch_catch_mode_hardware_test` (see its README,
"Raw data readout"). Each reading prints two blocks, coarse then fine. Each block has an extra
`pass=` line after `actual_mm=`:

```
BEGIN
title=<batch title>
actual_mm=<actual distance>
pass=coarse odr=5
meas=<n> num_samples=<N> target=<0|1> range_mm=<mm|NA> amp=<u>
END
BEGIN
title=<batch title>
actual_mm=<actual distance>
pass=fine odr=6 band=<1|2|3> pad_mm=<0|1250|2500> coarse_mm=<mm>
meas=<n> num_samples=<N> target=<0|1> range_mm=<mm|NA> amp=<u>
IQ,...
END
```

`range_mm` is the sensor's range plus `TRANSMITTER_OFFSET` + `RECEIVER_OFFSET` (from
`sensor_offsets.h` in `sensor_calibration`, currently 23.25 mm each). Those offsets run from each
transducer to the base of its unit, where `actual_mm` is measured from. `coarse_mm` on the fine line
is the coarse pass's *uncorrected* range, because it is what the band and gate are computed from,
in the sensor's own time base.

Fine-pass error reasons, besides the usual `sender_not_ready` / `no_response` /
`iq_read_failed_<rc>`:
- `no_coarse_target`: the coarse pass found no target.
- `coarse_failed`: the coarse pass itself errored.
- `wrong_slot`: the data came from the other measurement slot.
- `fine_config_failed`: the pad or gate couldn't be written.

Host side:
The coarse block has only the summary line (the sensor's distance), with no `IQ` rows.

- `host/extract_measurements.py` writes `<title>_<n>_fine.csv` (coarse blocks have no I/Q, so they
  get a manifest row but no CSV). It adds
  `pass`, `band`, `pad_mm` and `coarse_mm` columns to `manifest.csv`, and takes `odr` from each block.
- `host/plot_readout.py <fine csv> --odr 6 --pad-mm <pad_mm> --op-freq <Hz>` plots a fine pass on
  an absolute distance axis.
