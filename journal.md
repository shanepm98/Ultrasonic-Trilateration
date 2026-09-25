# Project Journal
This doc is for briefly summarizing daily progress/thoughts/setbacks for future reference

## 9-24-2026
- Added `src/apps/pitch_catch_mode_hardware_test/receiver_readout/`, a raw I/Q dump variant of
  `receiver/` for offboard signal processing / bench tuning. Same sensor configuration as
  `receiver_loop.c` (measurement queue, `icu_gpt` algo/thresholds, `CH_MODE_TRIGGERED_RX_ONLY`),
  but on-demand rather than free-running: a full I/Q dump (up to `ICU_MAX_NUM_SAMPLES` samples x
  4 bytes, ~1.4 KB) doesn't fit inside the base receiver's 100ms trigger interval over a typical
  console baud rate, so this app idles for a line typed on the serial console, fires exactly one
  trigger, and dumps that measurement's raw I/Q trace as plain text
  (`IQ_BEGIN`/`IQ,<idx>,<i>,<q>`/`IQ_END`). Deliberately plain text, not a binary/COBS protocol
  like `automated_tuning`'s - no host-side parser exists yet, this is meant as a quick bench tool.
  Builds clean in the Docker toolchain; not yet run on hardware.

## 9-17-2026
- Implemented the `automated_tuning` host control script (`host/tuner.py`, `serial_link.py`,
  `trial.py`, `tuning_search.py`, `cobs.py`) plus a pytest suite (31 tests, all hardware-
  independent: COBS round-trip, frame demuxing including the interleaved-telemetry case, trial
  statistics/scoring, the sample-index formula, JSON schema round-trip). Set up a venv +
  `requirements.txt` under `host/` per usual convention.
- Found and fixed a real firmware bug while implementing the host script (not on hardware yet):
  `AT_OP_ADD_SEGMENT_*` conflated "apply to the receiver's own sensor" with "relay to the
  transmitter" into a single shared array - the transmitter could never have received a working
  TX segment, since the receiver is RX-only and the transmitter needs TX+COUNT+RX. Fixed by
  adding an explicit `target` (LOCAL/REMOTE) field to the three segment opcodes
  (`AT_PROTOCOL_VERSION` bumped 1->2). Both firmware images rebuilt clean.
- First live hardware test of the whole system: hit a hang on `AT_OP_SET_MAX_RANGE`, every time,
  right after `AT_OP_GPT_ALGO_CONFIGURE` succeeded. Two theories (a SonicLib segment-shrink loop
  in `ch_common_meas_set_num_samples`, and an uninitialized-measurement corruption from calling
  `ch_meas_reset()` before any `ch_meas_init()`) were both ruled out by reading the vendored
  SonicLib source directly - both turned out to be pure host-side RAM operations with no
  possibility of hanging.
- Actual root cause, found by reading `at_cfg_meas_reset()` closely: it called
  `xSemaphoreTake(g_trigger_cycle_mutex, ...)` but never checked the return value, so a timed-out
  acquisition still fell through to call `ch_set_mode()`/`ch_meas_reset()` on the sensor without
  actually holding the lock - racing directly against the trigger loop's own in-flight SPI
  activity. Fixed by checking the return value and returning `AT_STATUS_ERR` on timeout instead.
- Added temporary debug instrumentation to confirm the fix: `rpc_dispatch.c` prints
  `[dbg] opcode=... enter/exit` around every RPC call via `esp_rom_printf`, wrapped in explicit
  `0x00` delimiters (`dbg_print()`) so it can't corrupt the real COBS-framed response that
  follows it on the wire - the first version of this without NUL-wrapping did exactly that,
  breaking even the `HELLO` handshake. `serial_link.py`'s reader was temporarily changed to print
  any bytes that fail to parse as a frame, so the breadcrumbs are visible from `tuner.py`.
- Reflash-and-retest confirmed the semaphore fix: the full reconfigure sequence (11 opcodes) now
  completes cleanly with `status=0` on every call, across all three Stage 1 ODR candidates - no
  hang. Also surfaced an unrelated, lower-priority finding: SonicLib's own `CH_LOG_INFO` logging
  (`CH_LOG_MODULE_LEVEL=2`) bypasses `esp_log_level_set()` entirely and leaks onto the wire
  regardless of our "UART0 belongs to the RPC link" design - COBS resync handles it today, but
  worth tightening later.
- New problem, not yet resolved: with the hang fixed, the search runs but reports
  `detection_rate=0.00` at a 250mm test distance - right at the edge of the ICU-20201's ~200mm
  rated minimum range. Receiver's own trigger loop/local config confirmed healthy (telemetry
  flows every trial); undetermined whether this is a near-field limitation of the seed config or
  an unvalidated wiring/ESP-NOW-pairing issue (this receiver+transmitter pair, on any firmware,
  has never been confirmed to produce a valid pitch-catch reading - see TODO.md).
- Paused here (mid-debug) to switch to another project. Temporary debug prints are still in the
  tree; see TODO.md for the exact resume plan.
- Scaffolded `src/apps/espnow_ftsp_test/` (previously an empty README-only stub) into a working
  first-pass FTSP-over-ESP-NOW capability test. Single symmetric app - both boards run identical
  firmware and elect a root by lowest MAC address at boot, rather than the sender/receiver split
  used by `pitch_catch_mode_hardware_test`. `ftsp_sync.c` fits a real least-squares regression of
  clock offset vs. local time from each root SYNC beacon; `gpio_strobe.c` uses `esp_timer` (not
  `vTaskDelay`) to strobe GPIO25 on 100ms boundaries of the synchronized root clock, re-deriving
  the deadline from the freshest fit every cycle. Builds clean in the Docker toolchain; not yet
  run on hardware - see TODO.md for the bench-tuning list.
- First on-hardware run of `espnow_ftsp_test`: both boards strobe GPIO25, but with a severe and
  variable delta between the two edges (one scope measurement: 8ms). Too large to be clock-skew
  drift and too variable to be a fixed-offset bug, which pointed at a jitter source. Root cause:
  `espnow_link_init()` never disabled WiFi power save, so STA mode defaulted to
  `WIFI_PS_MIN_MODEM` - sleeps the radio between operations and wakes it on demand, a known source
  of multi-ms variable latency on `esp_now_send()`/recv-callback dispatch that directly corrupts
  every FTSP timestamp (all taken at the API call site). Added `esp_wifi_set_ps(WIFI_PS_NONE)`.
  Builds clean; re-test on hardware is next (see TODO.md).
- Reflashed both boards with the power-save fix plus new per-packet reception logging. Confirmed
  the radio link and election are solid - both boards' logs showed a continuous, bidirectional
  stream of received HELLO/SYNC packets and correct election (`role=FOLLOWER, root=<the other
  board's MAC>` on one side, matching `role=ROOT` on the other). Not a communication problem.
- Found the real cause of the remaining severe/variable strobe delta in the follower's own sample
  log: 3 of 4 early SYNC samples clustered tightly (~366us spread), but one was a ~13ms outlier -
  and it landed as one of only 2 points in the regression right when `gpio_strobe` started
  trusting the fit. A 2-point line fits both points exactly, so that single bad sample became the
  entire skew estimate - exactly the gap already flagged in the design doc (no outlier rejection).
  Fixed: raised the minimum sample count before a fit is trusted
  (`FTSP_MIN_SAMPLES_FOR_VALID`, 2->4) and added residual-based outlier rejection against the
  existing fit (`FTSP_OUTLIER_THRESHOLD_US=3000`) in `ftsp_regression_add_sample()`. Builds clean;
  re-test on hardware is next.
- Added a compile-time logging mute (`CONFIG_FTSP_MUTE_LOGS` via a new `main/Kconfig.projbuild`)
  to test whether blocking UART writes, not WiFi, were the dominant jitter source - each console
  line costs several ms of real wall-clock time, long enough to delay FreeRTOS scheduling and
  corrupt the `esp_timer_get_time()` calls this app depends on. Verified via `strings` on the
  built `.elf` that enabling it genuinely strips the log format strings at compile time.
- Redesigned the strobe rendezvous per a design change requested mid-session: instead of each
  board independently rounding its own clock estimate up to the next 100ms boundary, the root now
  explicitly picks and broadcasts the next strobe instant in every SYNC packet
  (`next_strobe_root_us`), and followers copy it verbatim rather than re-deriving it - moves the
  "which boundary is next" decision onto the root's own, definitionally-accurate clock instead of
  a follower's noisier real-time estimate. Old independent-derivation logic kept as a fallback for
  when no fresh announcement is available yet. Also matched `FTSP_SYNC_INTERVAL_MS` to the strobe
  period (200->100ms) so a fresh announcement covers every strobe cycle. Builds clean both with
  and without logging muted; re-test on hardware (all three fixes together) is next.

## 9-11-2026
- Implemented `src/apps/pitch_catch_mode_hardware_test/{sender,receiver}` (previously an empty
  scaffold). Both build clean independently in the Docker toolchain; not yet run on hardware.
- Key design decision, settled through discussion before writing code: the **receiver** owns the
  periodic trigger loop (`ch_group_trigger()`), not the sender - inverts SonicLib's "typical"
  AN-000175 pattern. The receiver is the fixed beacon and knows its own trigger instant exactly
  (no communication latency), which matters for time-of-flight. The sender never calls any
  trigger function; it configures `CH_MODE_TRIGGERED_TX_RX` once and idles.
- Added a second physical link, sender's INT2 (data-ready) -> receiver's GPIO33, so the receiver
  can check the sender is actually done with its previous measurement before retriggering, instead
  of firing blind on a fixed timer. Read directly via `driver/gpio.h`/`esp_driver_gpio`, not
  through the BSP (which is scoped to a board's own local sensor).
- Source-verification correction while planning: `CH_MODE_TRIGGERED_TX_RX` is not TX-only - per
  AN-000175 the sender still listens for its own echo, so its measurement queue needs a full
  TX+settle+RX segment set, same shape as the echo-mode app. The receiver, being RX-only, uses a
  count segment (sized to match the sender's TX burst duration) instead of a TX segment.
- Added `include/pitch_catch_common.h`, a header shared by both independent app builds
  (`INCLUDE_DIRS "../../include"`), holding every cross-board-critical constant (TX burst timing,
  ODR, max range, trigger cadence, the GPIO33 pin) as a single source of truth, instead of
  hand-synced `#define`s that could silently drift apart.
- Deferred (documented in the new README and TODO.md, not implemented): frequency matching and
  `ch_set_rx_pretrigger()` across the two independently-calibrated sensors - both only work within
  one shared `ch_group_t`, which doesn't exist across two separate boards.
- Next: get both boards on the bench, verify INT1/GPIO33 wiring continuity, tune RX gain/
  thresholds per sensor, and validate `CH_RANGE_DIRECT` against a tape-measure baseline.

## 9-10-2026
- The free-running echo-mode rangefinding loop ran successfully on real hardware - POST + rangefinding both verified working end to end.
- Renamed `src/apps/hardware_bringup` -> `src/apps/echo_mode_hardware_test` and added a `src/apps/pitch_catch_mode_hardware_test/{sender,receiver}` scaffold for the next milestone (two-sensor triggered pitch-catch). Docs/TODO/CLAUDE.md paths updated to match.

## 9-6-2026
- Housekeeping: restructured the repo into `src/components/` (invn-soniclib, soniclib_esp32_bsp, icu_post) + `src/apps/hardware_bringup/`; dropped ~1400 committed build artifacts from git. Docs/paths updated. Builds verified.
- Wrote the free-running rangefinding loop (`src/apps/hardware_bringup/main/rangefinder_loop.c`): builds a measurement queue, configures icu_gpt algo + thresholds, `ch_set_max_range(5000)`, `ch_set_freerun_interval(100)`, `ch_set_mode(CH_MODE_FREERUN)`, prints `ch_get_range()` from the data-ready callback. Builds clean; not yet run on hardware. TX/RX/threshold values are starting guesses to tune on the bench.
- BSP interrupt refactor: GPIO ISR now only notifies a dedicated task (`bsp_int_task`) which calls `ch_interrupt()` at task level; removed `USE_DEFERRED_INTERRUPT_PROCESSING`. This is the proper fix for the earlier "SPI in ISR context" watchdog panic and makes the runtime data-ready callback work.

## 9-5-2026
- Fixed FFC cable wiring problem (all pins mirrored- 12 should be 1, 11 should be 2, 10 should be 3, etc) and got POST running and passing successfully
- The BSP (`src/soniclib_esp32_bsp`) and the power-on self-test (`src/hardware_bringup`) build
cleanly. USB cable issue (2026-09-03) resolved. FFC pinout in the datasheet was wrong - pins had to be
reversed (1<->12, 2<->11, ...); after rewiring, stage 1 (SPI link) PASSES (CPU ID 0x2041, device
found, firmware programmed). See `docs/hardware_bringup.md`.
- Then hit an `Interrupt wdt timeout ... running in ISR context` during firmware start. Cause: the
BSP GPIO ISR calls `ch_interrupt()`, which (without `USE_DEFERRED_INTERRUPT_PROCESSING`) ran the
SPI-heavy `chdrv_int_callback_deferred()` inline in ISR context -> blocking `spi_device_transmit()`
deadlock. Fixed by adding `USE_DEFERRED_INTERRUPT_PROCESSING` to `invn-soniclib/CMakeLists.txt`
(2026-09-05). Re-flash and continue from stage 2.

## 9-3-2026
- Started hardware bring-up. Had Claude write a reusable power-on self-test (`src/hardware_bringup`) that checks SPI comms to the ICU-20201 via SonicLib `chdrv_prog_ping()`, then `ch_group_start()`, then an identity read-back. Builds clean. Also fixed the BSP SPI mode (0 -> 3, per DS-000478 / AN-000357) and swapped the INT1/INT2 roles (INT1 = trigger, INT2 = data-ready).
- Setback: could not flash the ESP32 - USB serial link is unreliable (esptool reaches the ROM but flash writes abort mid-transfer, boot log comes back with bytes duplicated ~80x). Board is fine (boots factory ESP-AT off 4MB flash). Almost certainly a bad USB cable; no spare on hand. Resuming once a cable is available - see `TODO.md` / `docs/hardware_bringup.md`.

## 7-22-2026
- Designed a circuit that can function as either master or slave beacon, designed the PCB, and ordered that + parts. Waiting on board and parts. Next step is to bring up the hardware and do a hardware test of the board support package

## 7-8-2026
- Had Claude assist in writing the SonicLib board support package for the ESP32 Devkit V1. This is the intermediate layer between the high-level SonicLib business logic and the low-level ESP32 hardware, basically the driver to control the pins and timers. It is NOT tested in hardware yet, although it is verified to build successfully. Also had claude write `docs/board_support_package.md` and update `TODO.md` with next steps for testing the BSP

## 7-5-2026
- Revised the test rig to use the newer ICU-20201 ultrasonic chip instead of the CH201. Redesigned the test rig so that each sensor has its own ESP32 for the sake of convenience and not running a ton of long wires

## 6-24-2026
- Finished and uploaded schematics for a CH201 test rig. Two CH201's operate in pitch-catch mode, controlled by an ESP32. Hardware has yet to be built and I still need to write the ESP32 firmware

## 6-2-2026
- Created this GitHub account

