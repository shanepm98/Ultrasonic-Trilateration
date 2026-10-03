# Pitch-Catch mode test using infrared trigger synchronization
Previous hardware tests have verified that two boards can operate in pitch-catch mode using a hardwired connection between sensor trigger pins.

In this iteration, the hardwired sensor trigger line is made wireless using infrared. Each board has both an IR LED and 38KHz IR demodulator, where the demodulator output triggers the ultrasonic sensor.

To begin a measurement, the receiver board pulses it's own IR LED with 5 milliseconds of a 38KHz 50% duty-cycle PWM signal to GPIO25. This IR signal triggers both its own IR demodulator as well as the IR demodulator on the sender board.
On both boards, the output of the demodulator triggers the ultrasonic sensor. This method is used to keep symmetric latency (and thus more simultaneous triggering) on both sender and receiver.


## Functional description
- Like in the `pitch_catch_mode_hardware_test`, the receiver controls the measurement loop
- The sender is triggered by the receiver
- 38KHz IR drive signal generated using the ESP32's RMT peripheral (hardware carrier modulation; the
  whole pre-burst/gap/trigger waveform is timed in hardware to 1 us)

## Receiver console menu

After POST the receiver shows a menu with the current IR timing. Press `1`/`2`/`3` to pick a mode;
in modes 2 and 3, `m` returns to the menu.

**1) IR timing tuner**: sets the IR trigger waveform used by every later trigger in modes 2 and 3.
1) `Enable pre-burst? (y/n)`. On `y`: `pre-burst ON time (us)`, then `pre-burst OFF time (us)`.
   Type a number and press Enter.
2) `trigger burst time (us)`, asked whether or not the pre-burst is enabled.
3) Then press **Enter** to save and return to the menu, **Space** to fire a test trigger (as many
   times as you like, for the oscilloscope), or **Backspace** to start the configuration over.

The waveform is: carrier ON for `pre_on_us`, OFF for `pre_off_us` (both only if the pre-burst is
enabled), then carrier ON for `trigger_us`. Each value is 1..100000 us (`IR_MAX_SEGMENT_US`).
Typed digits are echoed, Backspace deletes a digit while typing a number, and out-of-range or
empty entries re-prompt. Each test shot also triggers both sensors, and the tuner reports whether
the receiver's sensor answered (`receiver INT2: yes/NO`). The settings live in RAM only: on reboot
they reset to pre-burst disabled, trigger 5000 us (`IR_BURST_US`).

**2) Raw I/Q readout**: Enter fires one trigger, waits for INT2 (up to 500 ms), and prints the I/Q
dump (see "Output format"). On timeout it prints a warning instead.

**3) Distance**: Enter fires one trigger and prints one line with the sensor's own computed direct
pitch-catch range: `RANGE meas=<n> target=<0|1> range_mm=<mm|NA> amp=<u>`. This mode skips the
I/Q readout, so it responds faster.

Before each trigger the receiver discards any measurement a stray IR trigger produced since the
last request (logged as `discarding an unrequested measurement`). The tuner's test shots are
consumed the same way and never show up as a later reading.

## Status

**Verified on hardware (2026-09-28):** the end-to-end test passes - Enter on the receiver fires
the IR burst, both sensors trigger via their demodulators, and the receiver dumps the I/Q trace.
The trigger edge and LED polarity are confirmed (see "Bench notes" below). The trigger skew
between the two boards has not been measured yet.

## Layout

Same shape as `../pitch_catch_mode_hardware_test/` - two independent ESP-IDF apps, one per board,
each consuming the shared components in `../../components/`:

- `sender/` - sensor configured `CH_MODE_TRIGGERED_TX_RX`. Configures once, then idles; its IR
  demodulator fires the sensor. Sensor config identical to the wired test's `sender/`.
- `receiver/` - sensor configured `CH_MODE_TRIGGERED_RX_ONLY`. Owns the console menu and the
  measurement modes (`main/readout_loop.c`), the tuner dialog (`main/ir_tuner.c`), keypress input
  (`main/console_io.c`), and the RMT IR waveform driver (`main/ir_led.c`). Sensor config is
  identical to the wired test's `receiver_readout/`.
- `include/ir_common.h` - cross-board constants: the sensor values carried over unchanged from
  `pitch_catch_common.h` (`PC_TX_*`, `PC_ODR`, `PC_MAX_RANGE_MM`) plus the IR link
  (`IR_LED_GPIO`, `IR_CARRIER_HZ`, `IR_BURST_US` (default trigger length), `IR_MAX_SEGMENT_US`,
  `IR_RESPONSE_TIMEOUT_MS`, `IR_POWER_GPIO`).
- `host/` - `extract_measurements.py` / `plot_readout.py`, same as the wired test's (see
  "Output format" below).

## INT1 / GPIO2 is high-impedance

The demodulator output drives the sensor's INT1 pin, which is also wired to ESP32 GPIO2.
`chbsp_esp32_init()` leaves GPIO2 a push-pull output held high (the BSP's normal trigger-out
role), which would fight the demodulator pulling the line low. Both apps therefore call
`chbsp_group_set_int1_dir_in()` right after POST, before configuring the sensor, leaving GPIO2 a
plain input with no pulls. Neither app ever calls `ch_trigger()`/`ch_group_trigger()` - SonicLib's
trigger routines switch the pin back to an output.

## Output format

In mode 2, each Enter keypress prints, on success:

```
IQ_BEGIN meas=<n> num_samples=<N> target=<0|1> range_mm=<mm|NA> amp=<u> pre_on_us=<us> pre_off_us=<us> trig_us=<us>
IQ,<sample index>,<I>,<Q>
...
IQ_END
```

or on timeout, a `no INT2 within 500 ms` warning. The trailing `pre_on_us`/`pre_off_us`/`trig_us`
fields record the IR waveform the capture was taken with (`0` = pre-burst disabled); the host
parser ignores them. `IQ_BEGIN`/`IQ,`/`IQ_END` (and mode 3's `RANGE`) are plain `printf` lines (not
`ESP_LOGx`), so the host scripts ignore interleaved log output and menu text:

```
./host/extract_measurements.py capture.txt --prefix 2m -o readouts/2m
./host/plot_readout.py readouts/2m/2m_1.csv --open
```

## Build / flash / monitor

```sh
cd sender/      # or receiver/
./build.sh                       # idf.py build, in the espressif/idf Docker image
PORT=/dev/ttyUSB0 ./build.sh flash monitor
```

Console is UART0 at 115200 baud; log tags are `bringup-ir-tx`/`ir-pitch-catch-tx` (sender) and
`bringup-ir-rx`/`ir-pitch-catch-rx` (receiver).

## Wiring (per board)

Local SPI/INT wiring is the same as `../pitch_catch_mode_hardware_test/README.md`. There are no
board-to-board wires. Additions:

| Signal | ESP32 GPIO | Notes |
|---|---|---|
| IR LED drive | 25 | 38 kHz RMT carrier; active-high (GPIO high = LED on, idles low/off) - confirmed on hardware. Only the receiver drives it. |
| Demodulator out | - | to sensor INT1 (the INT1 net, 2.2k pull-up) |
| INT1 | 2 | high-impedance input; not driven by firmware |
| IR module power | 26 | driven high at the very start of `app_main()` (before POST) and never changed - `ir_power_on()` in `include/ir_common.h`. Both boards. |

## Bench notes

- **Signal polarity / trigger edge (confirmed 2026-09-28)**: the IR LED idles low (off), the
  demodulator output is active-low, and the ICU's INT1 trigger is active-low. So INT1 goes low,
  and the measurement starts, as soon as the demodulator detects the start of the IR burst. The
  timing reference is therefore the demodulator's burst-*start* latency, the same on both boards.
  SonicLib's own trigger is a microsecond-scale low pulse, while the demodulator holds INT1 low
  for roughly the whole trigger burst (5 ms by default). That works on hardware, and the burst length does not affect
  trigger timing. It only needs to exceed the demodulator's minimum burst length.
- **Stray IR**: ambient IR (remotes, sunlight flicker) can make a demodulator fire and trigger a
  measurement nobody asked for. The receiver drains any such measurement before each burst and
  logs `discarding an unrequested measurement`.
- **Terminal line endings**: a terminal that sends CR+LF for Enter delivers two Enter bytes;
  `console_getc()` treats a second Enter byte within 20 ms of the first as the same keypress.
