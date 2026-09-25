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
- [ ] `receiver_readout/` (raw I/Q dump variant of `receiver/`, for offboard tuning) -
      **implemented (2026-09-24), builds clean, not yet run on hardware.** On-demand (not
      free-running): type a line in the serial monitor to trigger one measurement and dump its
      raw I/Q trace as plain text (`IQ_BEGIN`/`IQ,`/`IQ_END`). See
      `src/apps/pitch_catch_mode_hardware_test/README.md` ("Raw data readout"). Next: flash and
      confirm the dump against a known target, then decide whether a host-side parsing script is
      worth writing.

## Wireless synchronization system
In the hardware tests with the V3 revision of the PCB, the sensor trigger lines are hardwired together between the boards.
The next step is to research and develop a wireless system for coordinating all sensors with precision. One promising option
is the ESP-NOW protocol for synchronizing timestamps between boards (e.g, Flooding time synchronization protocol, FTSP),
and then agreeing on a trigger time.

`src/apps/espnow_ftsp_test/` - **scaffolded (2026-09-17), not yet validated on hardware.** Single
symmetric app (both boards run identical firmware; role decided at runtime by lowest-MAC
election), unlike the sender/receiver split used by `pitch_catch_mode_hardware_test`. See
`src/apps/espnow_ftsp_test/README.md` for the architecture writeup. Builds clean in the Docker
toolchain.
- [x] First on-hardware run with two boards (2026-09-17): both strobe, but with a severe and
      variable delta between the two GPIO25 edges (one measurement: 8ms) - far too large to be
      clock-skew drift, and "variable" pointed at a jitter source rather than a fixed-offset bug.
- [x] Found and fixed: `espnow_link_init()` never disabled WiFi power save, so STA mode defaulted
      to `WIFI_PS_MIN_MODEM`, which sleeps the radio between operations and wakes it on demand - a
      well-known source of multi-millisecond, variable latency on `esp_now_send()`/recv-callback
      dispatch that corrupts every FTSP timestamp, since they're all taken at the API call site.
      Added `esp_wifi_set_ps(WIFI_PS_NONE)` right after `esp_wifi_start()`. Builds clean; **not
      yet re-tested on hardware.**
- [x] Reflashed both boards with the power-save fix plus per-packet reception logging
      (2026-09-17). Confirmed both directions of the radio link and election are solid - the
      follower's log showed a continuous stream of `"discovery: heard claim ..."` lines from the
      root, correct election (`role=FOLLOWER, root=<root's MAC>`), and the root's own log showed
      it hearing the follower's HELLO echoing the same claim back. Not an ESP-NOW/config problem.
- [x] Found and fixed the real cause of the remaining severe/variable delta: the follower's
      sample log (`local=... root=... offset=...`) showed 3 of 4 early samples clustered tightly
      (~366us spread - a good noise floor) but one sample was a ~13ms outlier, and it landed as
      one of only *2* points in the regression right when `gpio_strobe` started using the fit - a
      2-point line fits both points exactly, so that single bad sample became the entire skew
      estimate. Fixed in `main/ftsp_sync.h`/`.c`: raised the minimum sample count before a fit is
      trusted (`FTSP_MIN_SAMPLES_FOR_VALID`, 2->4) and added residual-based outlier rejection in
      `ftsp_regression_add_sample()` (`FTSP_OUTLIER_THRESHOLD_US=3000`) once a fit exists. Builds
      clean; **not yet re-tested on hardware.**
- [x] Added a compile-time console-logging mute (`main/Kconfig.projbuild`'s
      `CONFIG_FTSP_MUTE_LOGS`, sets `LOG_LOCAL_LEVEL=ESP_LOG_NONE` in all 4 of this app's `.c`
      files) to test the hypothesis that blocking UART writes - not WiFi - were the dominant
      jitter source: each console line costs several ms of real wall-clock time at typical baud
      rates, long enough to delay FreeRTOS scheduling and corrupt the very `esp_timer_get_time()`
      calls this app's accuracy depends on. Off by default; verified via `strings` on the built
      `.elf` that enabling it actually strips the format strings, not just runtime-filters them.
- [x] Changed the rendezvous design (2026-09-17): the root now explicitly picks and broadcasts the
      next GPIO-strobe instant (`ftsp_msg_sync_t.next_strobe_root_us`) in every SYNC packet,
      instead of each board independently rounding its own clock estimate up to the next
      100ms boundary. Followers copy the announced value verbatim and only translate it to local
      time via the regression; the old independent-derivation logic remains as a fallback for when
      no still-future announcement is available yet (startup, a dropped packet). Moves the "which
      boundary is next" decision onto the root's own, definitionally-accurate clock instead of a
      follower's noisier real-time estimate. Also reduced `FTSP_SYNC_INTERVAL_MS` 200->100 to
      match the strobe period, so a fresh announcement is available every strobe cycle rather than
      every other one. See the rendezvous note at the top of `main/ftsp_sync.h`.
- [ ] **Resume here**: reflash both boards with the outlier-rejection fix, the explicit-rendezvous
      protocol change, and logging muted, then re-measure the GPIO25-to-GPIO25 delta on the scope.
      This is the first hardware test of all three fixes together. If still off, re-enable logging
      (`CONFIG_FTSP_MUTE_LOGS=n`) to see the `next_strobe=` field in the follower's sample log and
      confirm it's actually arriving fresh and being honored (vs. hitting the fallback branch
      constantly, which would mean SYNC packets are being missed more than expected).
- [ ] Tune `FTSP_TABLE_SIZE`/`FTSP_HELLO_INTERVAL_MS`/`FTSP_SYNC_INTERVAL_MS`/`FTSP_ROOT_TIMEOUT_MS`
      (`main/ftsp_sync.h`) against the measured sync error.
- [ ] Investigate the source of the occasional ~13ms sample outlier itself (still present, just
      now filtered rather than explained) - software timestamps (`esp_timer_get_time()` at the
      `esp_now_send()`/recv-callback call sites) remain the likely cause even with power save
      disabled, since WiFi driver queuing/CSMA backoff jitter isn't compensated for. Investigate
      whether a hardware/MAC-layer timestamp is available, or whether
      `esp_now_register_send_cb()` gives a tighter "actually transmitted" timestamp than the
      pre-send read currently used.
- [ ] Add residual/outlier rejection to `ftsp_regression_add_sample()` (`main/ftsp_sync.c`) - the
      original FTSP paper discards high-residual points before fitting; this scaffold doesn't yet.
- [ ] Election/re-election logic has only been reasoned about for 2 nodes - untested for >2 nodes
      or network partitions.

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
