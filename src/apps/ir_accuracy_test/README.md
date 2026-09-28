# Pitch-Catch mode test using infrared trigger synchronization
Previous hardware tests have verified that two boards can operate in pitch-catch mode using a hardwired connection between sensor trigger pins.

In this iteration, the hardwired sensor trigger line is made wireless using infrared. Each board has both an IR LED and 38KHz IR demodulator, where the demodulator output triggers the ultrasonic sensor.

To begin a measurement, the receiver board pulses it's own IR LED with 5 milliseconds of a 38KHz 50% duty-cycle PWM signal to GPIO25. This IR signal triggers both its own IR demodulator as well as the IR demodulator on the sender board.
On both boards, the output of the demodulator triggers the ultrasonic sensor. This method is used to keep symmetric latency (and thus more simultaneous triggering) on both sender and receiver.


## Functional description
- Like in the `pitch_catch_mode_hardware_test`, the receiver controls the measurement loop
- The sender is triggered by the receiver
- 38KHz IR drive signal generated using the ESP32's LEDC peripheral

## Receiver loop
The receiver main loop can be summarized as follows:

1) Wait for ENTER keypress from host
2) Pulse GPIO25 with 5ms of a 38KHz 50% duty cycle signal to drive the IR LED
3) Wait for INT2 interrupt or 500ms, whichever comes first
4) If no INT2 within 500ms, print warning to console. If INT2 triggers first, read out the raw I/Q data from sensor and print to console with clear delimiters for easy post-processing into CSV
5) Repeat from step 1

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
- `receiver/` - sensor configured `CH_MODE_TRIGGERED_RX_ONLY`. Owns the Enter -> IR burst -> dump
  loop (`main/readout_loop.c`); the IR LED driver is `main/ir_led.c`. Sensor config, data-ready
  callback, and dump format identical to the wired test's `receiver_readout/`.
- `include/ir_common.h` - cross-board constants: the sensor values carried over unchanged from
  `pitch_catch_common.h` (`PC_TX_*`, `PC_ODR`, `PC_MAX_RANGE_MM`) plus the IR link
  (`IR_LED_GPIO`, `IR_CARRIER_HZ`, `IR_BURST_US`, `IR_RESPONSE_TIMEOUT_MS`).
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

Per Enter keypress the receiver prints, on success:

```
IQ_BEGIN meas=<n> num_samples=<N> target=<0|1> range_mm=<mm|NA> amp=<u>
IQ,<sample index>,<I>,<Q>
...
IQ_END
```

or on timeout, a `no INT2 within 500 ms` warning. `IQ_BEGIN`/`IQ,`/`IQ_END` are plain `printf`
lines (not `ESP_LOGx`), so the host scripts ignore interleaved log output:

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
| IR LED drive | 25 | 38 kHz LEDC carrier; active-high (GPIO high = LED on, idles low/off) - confirmed on hardware. Only the receiver drives it. |
| Demodulator out | - | to sensor INT1 (the INT1 net, 2.2k pull-up) |
| INT1 | 2 | high-impedance input; not driven by firmware |
| IR module power | 26 | driven high at the very start of `app_main()` (before POST) and never changed - `ir_power_on()` in `include/ir_common.h`. Both boards. |

## Bench notes

- **Signal polarity / trigger edge (confirmed 2026-09-28)**: the IR LED idles low (off), the
  demodulator output is active-low, and the ICU's INT1 trigger is active-low. So INT1 goes low,
  and the measurement starts, as soon as the demodulator detects the start of the IR burst. The
  timing reference is therefore the demodulator's burst-*start* latency, the same on both boards.
  SonicLib's own trigger is a microsecond-scale low pulse, while the demodulator holds INT1 low
  for roughly the whole 5 ms burst. That works on hardware, and the burst length does not affect
  trigger timing. It only needs to exceed the demodulator's minimum burst length.
- **Stray IR**: ambient IR (remotes, sunlight flicker) can make a demodulator fire and trigger a
  measurement nobody asked for. The receiver drains any such measurement before each burst and
  logs `discarding an unrequested measurement`.
