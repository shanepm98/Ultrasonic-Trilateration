# To - Do

## General housekeeping + Documentation
- [ ] Update and clean up this to-do. Lots of now-irrelevant details.
- [ ] Add schematic for the IR triggering circuit.
- [ ] Add schematic for the buffered 32.768KHz clock oscillator
- [x] Remove the obsolete wireless sync testing and auto-calibration tests
- [x] Move the shared sensor config (TX burst, ODR, max range) to `src/components/sensor_calibration/include/sensor_calibration.h`
- [ ] Switch `ir_accuracy_test/include/ir_common.h` to `sensor_calibration.h` - it still defines its own
	  older `PC_*` values (80 us burst, width 3, `CH_ODR_DEFAULT`)
- [ ] Revise the documentation to reflect new change in project scope, that being that we are now
	  only concerned with 2-dimensional positioning for this proof of concept. 
- [ ] Get website launched to document the process in more detailed writeups of each stage. 
- [ ] Write up final research paper detailing the entire project: motivation, theory of operation, hardware design,
	  software design, next steps, etc.

## Hardware Design & testing
- [ ] Bench-test to verify that clocking the ESP32 externally via GPIO33 works. 
- [ ] Change the terminology used on the solder bridges
- [ ] Add red/green LEDs for indicating operational status (power-on self-test result, runtime errors, etc)
- [ ] Design and test a new version of the IR trigger circuit that triggers on the tail end of the 
	  LED signal, hopefully this will have a more consistent latency since the automatic gain control 
	  is already adjusted (as the AGC adjustment seems to be the cause of the variable latency)
- [ ] Wire up and test the 32KHz buffered oscillator circuit. 
- [ ] Design the omni-directional transmitter (the object to be tracked). Current plan is to implement it as a hexagon
	  with an ultrasonic transmitter on each edge, and 360-degree-FoV IR emitter/demodulator pair on top.
- [ ] Add a 3.3v LDO regulator parallel to the ESP32, so that any relatively high-current loads (like the CC1101 or IR)
      have dedicated power supply and dont go through the ESP32's onboard regulator

## CC1101 sync trigger (`src/apps/cc1101_sync_trigger_test/`)
433 MHz async radio as the trigger instead of IR. Switched from OOK to ASK on 2026-10-13, because OOK was too noise-prone. Transmitter (RMT on GDO0, console menu) and
receiver (GDO2 to the scope, ESP-NOW remote config) both build (2026-10-11). Not yet run on
hardware. See `docs/cc1101_sync_trigger.md`.
- [x] First power-up: check each boot dump shows PARTNUM (0xF0) = 0x00, VERSION (0xF1) = 0x14, the
      written config, and MARCSTATE (0xF5) = 0x0D on the receivers. If `cc1101_init` times out,
      check CSn (GPIO25) wiring and that MISO goes low after CSn.
- [x] Check menu `3` / `5` end to end: a broadcast write shows up in a receiver's log and in its `DUMP`
      reply.
- [x] Scope the transmitter's GPIO27 against two or more receivers' GDO2 with `0xAAAAAA` at 10, 50,
      100 and 250 kBaud. Record the edge-to-edge skew between receivers (target well under 1 us).
- [x] Sweep AGCCTRL2 (0x03-0x07), AGCCTRL0 (0x91/0x92), and the RX bandwidth (MDMCFG4 high nibble)
      with menu `3`, and find the setting with the lowest skew.
- [x] Skew sweep (2026-10-25, `host/skew_sweep.py`): ASK/FSK/MSK × TX rate × RX data rate × RX filter
      × AGC. Best: ASK, receivers' data rate 160 kBaud (MDMCFG4 low nibble 0xC, MDMCFG3 0x93), AGC
      05/00/91 - p90 0.35-0.45 us. Results in `docs/cc1101_sync_trigger.md`.
- [ ] Flash the receivers with the SIDLE -> wait-for-IDLE fix (`receiver/main/main.c`); the transmitter
      already has it.
- [ ] Run another automated sweep (Claude hooked up to oscilloscope and transmitter), this time
      using synchronous tx/rx mode. Only do this if async alternatives have too much spurious output.
- [ ] Bake the recommended config into `cc_ask.c` (receivers MDMCFG4 0x2C or 0x8C, MDMCFG3 0x93,
      AGCCTRL2 0x05; transmitter 10 kBaud) so a reboot doesn't fall back to the old defaults.
      If using FSK instead, rename any reference to ASK->FSK.
- [ ] Move recommended config to reusable component.
- [x] Test the shortlist in `src/apps/cc1101_sync_trigger_test/configs_to_test.md` more thoroughly:
      100+ shots each, also at real node spacing. It holds the sweep's top 4, plus FSK with a 203 kHz
      filter and receivers at 160 kBaud, with the TX at 1 and 10 kBaud. 10 shots per config only
      resolves differences of about 0.1 us, so watch for whole-bit outliers too.
      Done 2026-10-26 (100 shots each): only FSK, TX 50k, 541 kHz, AGC C3/00/91 kept every shot
      under 1 us (max 0.87 us). See `docs/cc1101_skew_sweep_summary.md`.
- [x] 500 shots each of FSK C3 and ASK 541 kHz (2026-10-26, `src/apps/cc1101_sync_trigger_test/results_500shot_20261026.md`):
      FSK had 4 whole-bit outliers (up to 119 us) in 500 shots. ASK had none (max 1.36 us, p95 0.9 us).
- [ ] Bake ASK, TX 10k, receivers MDMCFG4 0x2C / MDMCFG3 0x93, AGC 05/00/91 into `cc_ask.c` unless
      the FSK outliers get explained and fixed.
- [ ] Look into FSK's one-bit outliers: in the 500-shot run they were evenly spaced, about 29 s apart.
      Check whether that lines up with Wi-Fi/ESP-NOW beacons, other 433 MHz traffic, or AGC.
- [ ] ASK has a constant -0.17 us median offset between the two receivers. Swap the receivers or the
      scope channels to see whether it can be calibrated out per node.
- [ ] Try RX data rates above 160 kBaud (e.g. 250 / 500 kBaud): the skew is still falling at the top
      of the swept range.
- [ ] ASK (2026-10-13): reflash both. Check the transmitter's dump shows PATABLE 34 C0 and that
      MARCSTATE (0xF5) = 0x13 (TX) while idle, and confirm on a receiver's GDO2 that the noise
      chatter is gone with no data being sent.
- [ ] ASK: re-run the skew measurement at 10-250 kBaud. Sweep the "0" level (0x3E: 0x34 / 0x1D /
      0x0E / 0x12) against the noise immunity and the edge skew. Re-sweep AGCCTRL2, starting from
      the OOK result 0xC3 (`AGC_config.txt`).
- [ ] If it works: route GDO2 to INT1 and add the CC1101 to the bus that `chbsp_esp32_init()` brings
      up (instead of `cc_ask_init()`'s own `spi_bus_initialize`).

## IR-triggered pitch-catch (`src/apps/ir_accuracy_test/`)
`sender/` + `receiver/` - wireless version of `receiver_readout`: the receiver's IR burst
(GPIO25, 38 kHz, 5 ms) triggers both sensors via each board's IR demodulator on INT1; GPIO2 is
high-impedance on both boards; GPIO26 held high powers the IR module. **Verified on hardware
(2026-09-28): end-to-end test passes.** See `src/apps/ir_accuracy_test/README.md`.
- [x] First on-hardware run (2026-09-28): receiver's IR burst triggers both sensors, and the
      receiver dumps the I/Q trace.
- [ ] Scope receiver GPIO25 (38 kHz, ~5 ms per Enter) and INT1 on both boards (clean low pulse,
      no contention from GPIO2).
- [x] Trigger edge (2026-09-28): LED idles low, demodulator and INT1 are both active-low, so the
      measurement starts on the falling edge at the start of the IR burst. The ~5 ms low pulse
      works; burst length doesn't affect trigger timing.
- [ ] Measure trigger skew between the two boards (INT1 falling edges on the scope) - this is the
      core accuracy number for this approach.
- [ ] Capture readouts at known distances and compare `range_mm` / waveforms against the hardwired
      `pitch_catch_mode_hardware_test` captures (`readouts/15ft`, `readouts/25ft`).
- [x] IR LED drive polarity (2026-09-28): active-high, idles low (off), as the firmware assumes.
- Receiver console menu (2026-10-02): `m` -> (1) IR timing tuner (optional pre-burst on/off +
  trigger-burst length, Space = test trigger), (2) raw I/Q readout, (3) distance (`RANGE` line).
  IR waveform moved from LEDC + busy-wait to RMT (exact to 1 us). **Builds clean, not yet run on
  hardware.**
  - [ ] Flash and walk through the menu: tuner prompts/echo, Backspace restart, Enter save, `m`
        from modes 2/3. Confirm the serial monitor's Enter doesn't fire twice.
  - [ ] Scope GPIO25 with a pre-burst (e.g. 600/600/5000 us): exact envelope, 38 kHz carrier,
        LED off between triggers.
  - [ ] Use the tuner to find the shortest reliable trigger burst and whether a pre-burst
        (demodulator AGC settling) tightens the trigger skew between the boards.

## Hardwired pitch-catch batch readout (`src/apps/pitch_catch_mode_hardware_test/receiver_readout/`)
Console now prompts for batch title / actual distance (mm) / reading count, then dumps N
`BEGIN`/`END` blocks (title + actual_mm + sensor summary + I/Q). `host/extract_measurements.py`
writes per-reading CSVs + `manifest.csv`. **Builds clean (2026-10-05), not yet run on hardware.**
- [x] Flash and walk a batch: prompt echo/Backspace, single Enter starts the batch (no double fire
      from CR+LF), and N blocks come out for N readings.
- [x] Check back-to-back readings don't hit `sender_not_ready`. If they do, measure how long the
      sender's INT2 (GPIO33) stays low and raise `SENDER_READY_TIMEOUT_MS`.
- [x] Capture batches at several known distances (e.g. 250-3000 mm) and plot `range_mm` vs
      `actual_mm` from `manifest.csv` to get the hardwired baseline error/offset.

## Two-pass pitch-catch (`src/apps/two-pass_pitch_catch_test/receiver_readout/`)
Coarse f_op/4 pass picks a band, then a fine f_op/2 pass with a padded window. **Builds clean
(2026-10-08), not yet run on hardware.** See the app's README.
- [x] Flash `sender` + `receiver_readout`. Check that the startup log shows meas 0 at ODR 5 and
      meas 1 at ODR 6, and that readings never come back `wrong_slot`.
- [x] Capture batches in each band (e.g. 1 m, 2.5 m, 4 m). Check that the fine pulse lands at
      sample ≈ (coarse_mm - pad_mm) / Δ_fine. If the fine `range_mm` is about `pad_mm` short,
      SonicLib isn't compensating for the pad - add it in software.
- [x] Check the mounting offsets (`TRANSMITTER_OFFSET` / `RECEIVER_OFFSET` in
      `sensor_calibration/include/sensor_offsets.h`, 23.25 mm each). They're added to the printed
      `range_mm`. Measure them on the units and see how much of the ranging offset they explain.
- [ ] Calibrate `TP_GATE_LEVEL` (and `TP_GATE_HALF_MM`) from the fine-pass I/Q. It is a placeholder.
- [ ] Watch for `sender_not_ready` on the fine trigger. If it shows up, raise `TP_PASS_GAP_MS`.
- [ ] Compare fine vs coarse `range_mm` spread at each distance, to see whether the extra
      resolution pays off.
- [ ] Once it's validated, port the flow to a live-distance receiver app. The copied `receiver/`
      was removed.

## Detection threshold calibration (`src/components/sensor_calibration/`)
8-segment `sc_rx_thresholds` fitted by `receiver_readout/host/calibrate_thresholds.py` from the
2026-10-07 captures (450 us / PW 4 TX burst, ODR f_op/4, 0.5-5 m). See `docs/threshold_calibration.md`.
Replay: 50/50 in all 14 batches, including 5 m at 30°, with 0 false detections.
**Used by the two-pass `receiver_readout`'s coarse pass (2026-10-09); not yet tested on hardware.**
- [ ] Finish the angle sweep: both boards turned up to 45° at each distance (especially 4-5 m at
      45°), then re-run the script (`--dry-run` first).
- [ ] Re-check the 5 m distance: its pulse arrives 6 samples (~93 mm) early. Every other batch is
      within ±2 samples.
- [ ] Integrate into `receiver_readout` (REQUIRES `sensor_calibration`, pass `&sc_rx_thresholds` to
      `icu_gpt_algo_configure()`), re-run batches and check the `target=1` rate in `manifest.csv`.
      Then do the same for `receiver/`. `ir_accuracy_test` still uses the old 80 us / ODR 4 settings
      (`ir_common.h`), so it needs those updated first.
- [ ] Pin down the threshold scale k (|I,Q| per threshold unit). The fits so far are 1.44-1.50
      (2026-10-07) and 1.45-1.70 (2026-10-05), so the detector isn't exactly |I,Q|/k. Capture a batch
      with a known-flat threshold near the signal level to measure it directly.
- [ ] Ranging offset. With the 450 us burst the firmware `range_mm` is 437 / 1430 / 2922 / 4022 at
      0.5 / 1.5 / 3 / 4 m. Detection fires on the pulse's 10-sample rising edge, so the error depends
      on signal strength. Find a correction, e.g. a fixed offset plus a dependence on amplitude.
- [ ] Update `extract_measurements.py` / `plot_readout.py` `--odr` defaults (still 4) to match `PC_ODR` (5).

## General performance tweaks and features
- [ ] Raise SPI clock if needed. Running at 1MHz right now, can go up to 13MHz
- [ ] Tune the `RF_*` `#define`s at the top of `rangefinder_loop.c` against real targets:
      thresholds (false targets vs missed targets), `RF_TX_PULSE_US` (far-target echo strength),
      `RF_RX_GAIN_REDUCE` / `RF_RINGDOWN_CANCEL_SAMPLES` (near-field / ringdown false targets).
- [ ] Optional, improves close-range accuracy: `ch_set_init_firmware(&dev, icu_init_init)` before
      `ch_group_start()` + `ch_meas_optimize()` after building the queue (auto-dampens ringdown;
      ~250 ms, run once). `icu_init` is already compiled in.


## Self-mapping relative coordinate system
The stationary beacons should be able to coordinate with each other and use distance from one another to establish their own local,
relative coordinate system in which to locate the mobile client. More research needed into the feasibility of this.



