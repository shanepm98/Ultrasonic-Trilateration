# Hardware bring-up

ESP-IDF app for bringing up the EV_MOD_ICU-20201 ultrasonic module wired to an ESP32 DevKitV1.
It consumes the shared components in `../../components/`: `soniclib_esp32_bsp` (the BSP),
`invn-soniclib` (vendored SonicLib), and `icu_post` (the self-test below).

## Power-on self-test — the `icu_post` component

`../../components/icu_post/` (`icu_post.{c,h}`) — a modular, reusable check that the ESP32 can
talk to the sensor over SPI. SonicLib-level only (no direct esp-idf calls beyond `esp_log`), so
any app can call `post_run()` at boot regardless of BSP/wiring changes. See
`../../components/icu_post/README.md` for the API summary and `../../../docs/icu_post.md` for the
full stage-by-stage reference.

`main/post_main.c` is the bring-up harness: it does `ch_group_init` -> `ch_init` ->
`chbsp_esp32_init` -> `post_run` -> `post_report`, retrying every 5 s on failure. Once the POST
passes it calls `rangefinder_run()` (below). It is **not** the production `app_main`.

## Rangefinding loop — `main/rangefinder_loop.{c,h}`

`rangefinder_run()` configures the sensor for **free-running mode at 5 m** full-scale range
(10 Hz) and prints each measured one-way distance to the console (`rangefinder: #N <dist> mm`, or
`no target`). The transmit/receive/threshold `#define`s at the top of `rangefinder_loop.c` are
bring-up starting points to tune on the bench. Data-ready is handled from the BSP's `bsp_int_task`
(task level), so the callback reads `ch_get_range()` directly.

## Build / flash / monitor

```sh
./build.sh                       # idf.py build, in the espressif/idf Docker image
./build.sh flash monitor         # needs the board on $PORT (default /dev/ttyUSB0)
PORT=/dev/ttyACM0 ./build.sh flash monitor
```

`build.sh` mounts the repo `src/` directory so the shared components under `../../components/`
resolve. Console output is on UART0 (USB serial) at 115200 baud; watch for the `POST` /
`bringup` log tags.

## Wiring

Per `../../components/soniclib_esp32_bsp` and `CAD/Rangefinder_test_schematic_v3`:

| Signal | ESP32 GPIO | ICU-20201 | Notes |
|---|---|---|---|
| MOSI | 23 | pin 3 | |
| MISO | 19 | pin 4 | |
| SCLK | 18 | pin 2 | SPI mode 3 (CPOL=1, CPHA=1) |
| CS   | 5  | pin 5 | manual/software chip-select |
| INT1 | 2  | pin 6 | hardware trigger out (`CHIRP_SENSOR_TRIG_PIN=1`); open-drain, 2.2k pull-up |
| INT2 | 4  | pin 7 | data-ready interrupt in (`CHIRP_SENSOR_INT_PIN=2`); open-drain, 2.2k pull-up |

VDD = 1.8V regulated; VDDIO may be 3.3V (direct to the ESP32).
