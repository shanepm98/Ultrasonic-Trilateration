# Pitch-catch hardware bring-up

Two independent ESP-IDF apps, each flashed to its own ESP32 + ICU-20201 board, doing **direct
pitch-catch** ranging: the tracked object emits (`sender/`), a fixed beacon listens
(`receiver/`) - see the top-level project guide. Each consumes the shared components in
`../../components/`: `soniclib_esp32_bsp` (the BSP), `invn-soniclib` (vendored SonicLib), and
`icu_post` (the self-test), the same way `echo_mode_hardware_test` does. `include/` holds a
header shared by both apps (see below).

- `sender/` - sensor configured `CH_MODE_TRIGGERED_TX_RX`. Configures once, then idles.
- `receiver/` - sensor configured `CH_MODE_TRIGGERED_RX_ONLY`. Owns the periodic trigger loop.

`sender/` and `receiver/` build and flash completely independently - they don't depend on each
other's directory, only on `../../components/` and `../include/`.

## Trigger-sync architecture

Per `../../components/soniclib_esp32_bsp`'s doc and `CAD/Rangefinder_test_schematic_v3`, the two
boards' sensor INT1 (trigger) pins are physically wired together, open-drain with a shared
pull-up.

**The receiver owns the periodic trigger loop, not the sender** - an inversion of SonicLib's
"typical" example in AN-000175, where the TX/RX node usually drives the periodic timer. Here, the
receiver (`receiver_loop.c`) calls `ch_group_trigger()` on a fixed cadence, which pulses its own
GPIO2/INT1 line; because that line is shared with the sender's board, the same edge simultaneously
fires the sender's sensor. The receiver is the fixed beacon, so it knows its own trigger instant
exactly, with zero communication latency - that matters for computing time-of-flight.

The sender's firmware (`sender_loop.c`) never calls `ch_trigger()`/`ch_group_trigger()` - it only
configures its sensor for `CH_MODE_TRIGGERED_TX_RX` and reacts passively to externally-driven
trigger edges. This "passive" contract is enforced purely at the app level (never calling the
trigger functions), not by the GPIO becoming an input: `CHIRP_SENSOR_TRIG_PIN` (1) and
`CHIRP_SENSOR_INT_PIN` (2) differ on this board, so SonicLib's driver never reverts the TRIG pin
to an input after use - GPIO2 is configured push-pull output at boot regardless of whether the app
ever triggers. **Bench-check item (not a firmware fix)**: confirm the shared INT1 net tolerates
this - i.e. that only one side (the receiver) ever actually drives it low.

## Sender-ready handshake

The sender's sensor INT2 (data-ready, GPIO4 locally) is additionally wired to the receiver's
**GPIO33**, over a second physical link separate from the receiver's own local sensor INT2. The
receiver polls this pin (`gpio_get_level()`, plain ESP-IDF `driver/gpio.h`, not the `chbsp_*` BSP
API - that's scoped to a board's own local sensor) immediately before each trigger, and skips
(logging a warning) if the sender still shows busy, rather than firing on a fixed timer regardless
of sender state.

The sender's own application never touches its INT2 data (no `ch_io_int_callback_set()` call) -
but the BSP still services that interrupt transparently at the driver level (`bsp_int_task` ->
`ch_interrupt()` runs on every INT2 edge regardless of whether an app callback is registered),
which is exactly the low/high transition the receiver's GPIO33 tap observes. So the sender needs
no code to support this handshake - it falls out of the BSP's existing interrupt servicing.

## Direct pitch-catch range

The receiver's data-ready callback reads `ch_get_range(dev, CH_RANGE_DIRECT)` - the direct
pitch-catch range, since the sender and receiver are separate physical objects (not a reflected/
co-located setup). This is the only measurement the system reports; the sender produces no local
data (see above).

## `include/pitch_catch_common.h`

Cross-board constants - values where the sender's and receiver's independent calculations must
numerically agree for the protocol to work - live in `include/pitch_catch_common.h`, included by
both apps' `main/CMakeLists.txt` (`INCLUDE_DIRS "." "../../include"`), instead of being
hand-duplicated as matching `#define`s in two independent builds:

- `PC_TX_PULSE_US` / `PC_TX_PULSE_WIDTH` / `PC_TX_PHASE` - the sender's TX burst; the receiver's
  count segment must be sized from `PC_TX_PULSE_US` too (AN-000175: the count segment cycle count
  should match the other sensor's transmit cycle count).
- `PC_ODR`, `PC_MAX_RANGE_MM` - so both sensors interpret sample timing and range the same way.
- `PC_TRIGGER_INTERVAL_MS`, `PC_RESPONSE_TIMEOUT_MS` - the receiver's trigger cadence.
- `PC_SENDER_READY_GPIO` - the receiver's GPIO33 pin assignment for the handshake above.

Per-sensor bench-tunable calibration (RX gain/attenuation, detection thresholds, ringdown/
static-filter samples) is **not** here - those legitimately differ per physical sensor unit and
stay local to each app's own `*_loop.c`.

## Known limitations / future work

- **Frequency matching** between the two independently-calibrated sensors is not implemented.
  `ch_group_set_frequency(grp, CH_OP_FREQ_USE_AVG)` only averages frequencies within one shared
  `ch_group_t` and can't be used across two separate boards/groups.
- **Receive pre-triggering** (`ch_set_rx_pretrigger()`, a 600us ringdown-settle convenience for
  close-range accuracy) is likewise scoped to one shared `ch_group_t` and isn't usable cross-board.

See `TODO.md` for the first on-hardware bring-up checklist (bench tuning, wiring continuity,
`CH_RANGE_DIRECT` validation against a measured baseline).

## Build / flash / monitor

Each app builds and flashes independently, same pattern as `echo_mode_hardware_test`:

```sh
cd sender/      # or receiver/
./build.sh                       # idf.py build, in the espressif/idf Docker image
./build.sh flash monitor         # needs that board on $PORT (default /dev/ttyUSB0)
PORT=/dev/ttyACM0 ./build.sh flash monitor
```

`build.sh` mounts the repo `src/` directory so the shared components under `../../../components/`
and the common header under `../../include/` resolve. Console output is on UART0 (USB serial) at
115200 baud; watch for the `bringup-tx`/`pitch-catch-tx` (sender) or `bringup-rx`/`pitch-catch-rx`
(receiver) log tags.

## Wiring

Per `../../components/soniclib_esp32_bsp` and `CAD/Rangefinder_test_schematic_v3`, **each board**
(sender and receiver) has the same local SPI wiring:

| Signal | ESP32 GPIO | ICU-20201 | Notes |
|---|---|---|---|
| MOSI | 23 | pin 3 | |
| MISO | 19 | pin 4 | |
| SCLK | 18 | pin 2 | SPI mode 3 (CPOL=1, CPHA=1) |
| CS   | 5  | pin 5 | manual/software chip-select |
| INT1 | 2  | pin 6 | hardware trigger out (`CHIRP_SENSOR_TRIG_PIN=1`); open-drain, 2.2k pull-up |
| INT2 | 4  | pin 7 | data-ready interrupt in (`CHIRP_SENSOR_INT_PIN=2`); open-drain, 2.2k pull-up |

Plus two board-to-board links, specific to this pitch-catch pair:

| Link | Sender pin | Receiver pin | Purpose |
|---|---|---|---|
| Shared trigger | INT1 (GPIO2) | INT1 (GPIO2) | Receiver's `ch_group_trigger()` fires both sensors |
| Sender-ready | INT2 (GPIO4) | GPIO33 | Receiver checks sender's data-ready state before retriggering |

VDD = 1.8V regulated; VDDIO may be 3.3V (direct to the ESP32). Both boards need this rail
independently.
