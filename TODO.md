# To - Do

## General housekeeping + Documentation
- [ ] Update and clean up this to-do. Lots of now-irrelevant details.
- [ ] Add schematic for the IR triggering circuit.
- [ ] Add schematic for the buffered 32.768KHz clock oscillator
- [x] Remove the obsolete wireless sync testing and auto-calibration tests

## Hardware Design & testing
- [ ] Change the terminology used on the solder bridges
- [ ] Add red/green LEDs for indicating operational status (power-on self-test result, runtime errors, etc)
- [ ] Add 


## ESP32 SonicLib BSP + echo-mode hardware test

`src/apps/echo_mode_hardware_test/` (renamed from `hardware_bringup`) - POST + free-running
single-sensor rangefinding loop (`main/rangefinder_loop.c`). **Verified running successfully on
real hardware (2026-09-10).**

### Next - tune, then move on to pitch-catch
- [ ] Tune the `RF_*` `#define`s at the top of `rangefinder_loop.c` against real targets:
      thresholds (false targets vs missed targets), `RF_TX_PULSE_US` (far-target echo strength),
      `RF_RX_GAIN_REDUCE` / `RF_RINGDOWN_CANCEL_SAMPLES` (near-field / ringdown false targets).
- [ ] Optional, improves close-range accuracy: `ch_set_init_firmware(&dev, icu_init_init)` before
      `ch_group_start()` + `ch_meas_optimize()` after building the queue (auto-dampens ringdown;
      ~250 ms, run once). `icu_init` is already compiled in.
- [ ] SPI clock is a conservative 1 MHz (`BSP_SPI_CLOCK_HZ` in `esp32_bsp_internal.h`); raise it
      now that framing is confirmed working (datasheet max 13 MHz).
- [ ] Longer-term / only if needed: non-blocking I/Q readout (`chbsp_spi_mem_read_nb`) is
      unimplemented (falls back to the `chbsp_dummy.c` error stub) - add it if higher-throughput
      non-blocking reads become necessary.

## Pitch-catch mode hardware test

`src/apps/pitch_catch_mode_hardware_test/{sender,receiver}` - **implemented (2026-09-11), not yet
validated on hardware.** Two sensors: one `CH_MODE_TRIGGERED_TX_RX` (sender), one
`CH_MODE_TRIGGERED_RX_ONLY` (receiver), synchronized via the shared INT1 trigger wire between
boards (see PCB v3 note below). Free-running mode cannot be used for multi-sensor sync - see
AN-000175 §2.4/§2.7. Reuses `src/components/{invn-soniclib,soniclib_esp32_bsp,icu_post}` the same
way `echo_mode_hardware_test` does. See `src/apps/pitch_catch_mode_hardware_test/README.md` for
the full architecture writeup.
- [x] `post_run()` confirmed sensor-agnostic (stage 3 identity checks don't assume TX/RX role) -
      reused unmodified by both apps.
- Architecture notes (see README for detail):
  - **Receiver owns the trigger loop**, not the sender - inverts AN-000175's typical pattern.
    The receiver calls `ch_group_trigger()` on a fixed cadence, which fires both sensors over the
    shared INT1 wire, so it knows its own trigger instant with zero communication latency.
  - **Sender-ready handshake**: the sender's sensor INT2 is additionally wired to the receiver's
    GPIO33 (plain digital input, not through the BSP), which the receiver checks before each
    retrigger instead of firing blind on a timer.
  - **Common header**: `src/apps/pitch_catch_mode_hardware_test/include/pitch_catch_common.h`
    holds every cross-board-critical constant (TX burst timing, ODR, max range, trigger cadence,
    the GPIO33 pin) as a single source of truth, instead of hand-synced `#define`s in each app.
- [ ] Deferred: frequency matching across the two independently-calibrated sensors -
      `ch_group_set_frequency(CH_OP_FREQ_USE_AVG)` only works within one shared `ch_group_t`, not
      across two separate boards. No cross-board equivalent implemented.
- [ ] Deferred: `ch_set_rx_pretrigger()` (600us ringdown-settle convenience) is likewise scoped to
      one shared `ch_group_t` and unusable cross-board as-is.
- [ ] First on-hardware pitch-catch run: tune `RX_*`/threshold values on both boards
      independently, verify INT1 and GPIO33 wiring continuity, validate `CH_RANGE_DIRECT` against
      a tape-measure baseline, confirm the GPIO33 readiness gate actually prevents
      double-triggering when the sender responds slowly.
- [ ] `receiver_readout/` (raw I/Q dump variant of `receiver/`, for offboard tuning) -
      **implemented (2026-09-24), builds clean, not yet run on hardware.** On-demand (not
      free-running): type a line in the serial monitor to trigger one measurement and dump its
      raw I/Q trace as plain text (`IQ_BEGIN`/`IQ,`/`IQ_END`). See
      `src/apps/pitch_catch_mode_hardware_test/README.md` ("Raw data readout"). Next: flash and
      confirm the dump against a known target, then decide whether a host-side parsing script is
      worth writing.

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
- [ ] Add KiCad schematics for the IR triggering hardware

## Self-mapping relative coordinate system
The stationary beacons should be able to coordinate with each other and use distance from one another to establish their own local,
relative coordinate system in which to locate the mobile client. More research needed into the feasibility of this.



