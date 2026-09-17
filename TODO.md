# To - Do

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

## Wireless synchronization system
In the hardware tests with the V3 revision of the PCB, the sensor trigger lines are hardwired together between the boards.
The next step is to research and develop a wireless system for coordinating all sensors with precision. One promising option
is the ESP-NOW protocol for synchronizing timestamps between boards (e.g, Flooding time synchronization protocol, FTSP),
and then agreeing on a trigger time.

## Automated sensor tuner (`src/apps/automated_tuning/`)
Protocol, firmware, and host control script all implemented (2026-09-17); currently in first
hardware bring-up / debugging, **paused mid-session** to work on something else. See
`src/apps/automated_tuning/README.md` for the full writeup.

- [x] Binary RPC protocol (`include/at_protocol.h`, `host/protocol.py`), receiver/transmitter
      firmware, and the automatic tuning search (`host/tuning_algorithm.md`, `host/tuner.py`).
- [x] Fixed during bring-up: `AT_OP_ADD_SEGMENT_*` conflated "apply to the receiver's own sensor"
      with "relay to the transmitter" into one array - the transmitter could never have gotten a
      real TX segment. Added an explicit LOCAL/REMOTE `target` field (`AT_PROTOCOL_VERSION`
      bumped 1->2).
- [x] Fixed during bring-up: `at_cfg_meas_reset()` ignored `xSemaphoreTake()`'s return value, so
      a timed-out lock acquisition still fell through to touch the sensor without holding it.
      This was the root cause of a `SET_MAX_RANGE` hang seen on the first live test - confirmed
      fixed via debug logging (full reconfigure sequence now completes cleanly, status=OK on
      every opcode).
- [ ] **Resume here**: with the hang fixed, the automatic search now runs cleanly but reports
      `detection_rate=0.00` (no target ever detected) at a 250mm test distance - very close to
      the ICU-20201's ~200mm datasheet-rated minimum range. The receiver's own trigger loop and
      local sensor config are confirmed healthy (telemetry flows every trial), so this looks like
      either a near-field/minimum-range limitation of the seed config, or an unvalidated
      transmitter/pairing/wiring issue rather than an RPC bug. Next steps when resuming:
      1. Retest at a more typical distance (1-3m) to rule out the near-field explanation.
      2. Confirm whether this receiver+transmitter pair has ever produced a valid pitch-catch
         reading on *any* firmware - `pitch_catch_mode_hardware_test`'s own to-do below still
         lists "first on-hardware pitch-catch run" as unvalidated, which would point at basic
         wiring/ESP-NOW pairing rather than anything in `automated_tuning`.
- [ ] **Temporary debug code still in the tree** - remove once the detection issue above is
      resolved: `receiver/main/rpc_dispatch.c`'s `dbg_print()` (NUL-delimited `esp_rom_printf`
      breadcrumbs around every RPC call) and the matching `[raw/malformed]` print in
      `host/serial_link.py`'s reader loop.
- [ ] Separate, lower-priority bug found during the same debug session: SonicLib's own
      `CH_LOG_INFO`-level logging (`CH_LOG_MODULE_LEVEL=2` in `invn-soniclib/CMakeLists.txt`)
      bypasses `esp_log_level_set()` entirely, so it isn't actually silenced by the receiver's
      "UART0 belongs to the RPC link now" design - COBS resync papers over it today, but lowering
      `CH_LOG_MODULE_LEVEL` (e.g. to ERROR) would close the gap properly.
- [ ] Minor polish once the above is resolved: telemetry's `amplitude` field is force-zeroed
      whenever `have_target=false` (`trigger_loop.c`'s `data_ready_cb`), which hides "how close
      was the signal to threshold" diagnostic info that would help live-tune borderline configs.


## Self-mapping relative coordinate system
The stationary beacons should be able to coordinate with each other and use distance from one another to establish their own local,
relative coordinate system in which to locate the mobile client. More research needed into the feasibility of this.

## PCB Design
- [ ] Change the terminology used on the solder bridges
- [ ] Add red/green LEDs for indicating operational status (power-on self-test result, runtime errors, etc)
