# Hardware bring-up

ESP-IDF project for bringing up the EV_MOD_ICU-20201 ultrasonic module wired to an ESP32
DevKitV1, using the SonicLib board support package in `../soniclib_esp32_bsp`.

## Power-on self-test (`main/power_on_self_test.{c,h}`)

A modular, reusable check that the ESP32 can talk to the sensor over SPI. It is SonicLib-level
only (no direct esp-idf calls) so later firmware iterations can call `post_run()` at boot
regardless of BSP/wiring changes.

| Stage | What it does | What a pass proves |
|---|---|---|
| 1. SPI link | `chdrv_prog_ping()` - resets the sensor via its `SYS_CTRL` register and reads back `CPU_ID_HI` over SPI, checking it equals `SHASTA_CPU_ID_HI_VALUE` (0x2041) | Wiring, chip-select, **SPI mode 3**, bit order and framing are all correct |
| 2. Program + start | `ch_group_start()` then `ch_sensor_is_connected()` | Firmware downloads, frequency locks, RTC calibrates |
| 3. Sensor identity | reads part number / serial / fw version / operating frequency / RTC cal result and sanity-checks them | The programmed sensor reports sane values (part `20201`, f_op ~85 kHz) |

`main/post_main.c` is the bring-up harness: it does `ch_group_init` -> `ch_init` ->
`chbsp_esp32_init` -> `post_run` -> `post_report`, retrying every 5 s on failure, then idles.
It is **not** the production `app_main`.

### API

```c
post_config_t cfg;
post_config_default(&cfg);          // full test; set cfg.run_group_start = false for SPI-only
post_result_t res;
bool ok = post_run(&grp, &dev, &cfg, &res);
post_report(&res);                  // dump the result table to the console any time
```

## Build / flash / monitor

```sh
./build.sh                       # idf.py build, in the espressif/idf Docker image
./build.sh flash monitor         # needs the board on $PORT (default /dev/ttyUSB0)
PORT=/dev/ttyACM0 ./build.sh flash monitor
```

`build.sh` mounts the repo `src/` directory so the shared `soniclib_esp32_bsp` and
`invn-soniclib` components resolve. Console output is on UART0 (USB serial) at 115200 baud;
watch for the `POST` / `bringup` log tags.

## Wiring

Per `../soniclib_esp32_bsp` and `CAD/Rangefinder_test_schematic_v3` (SPI unchanged from the
earlier revision):

| Signal | ESP32 GPIO | ICU-20201 | Notes |
|---|---|---|---|
| MOSI | 23 | pin 3 | |
| MISO | 19 | pin 4 | |
| SCLK | 18 | pin 2 | SPI mode 3 (CPOL=1, CPHA=1) |
| CS   | 5  | pin 5 | manual/software chip-select |
| INT1 | 2  | pin 6 | hardware trigger out (`CHIRP_SENSOR_TRIG_PIN=1`); open-drain, 2.2k pull-up |
| INT2 | 4  | pin 7 | data-ready interrupt in (`CHIRP_SENSOR_INT_PIN=2`); open-drain, 2.2k pull-up |

VDD = 1.8V regulated; VDDIO may be 3.3V (direct to the ESP32).
