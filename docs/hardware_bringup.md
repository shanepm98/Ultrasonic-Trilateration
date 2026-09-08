# Hardware bring-up

App: `src/apps/hardware_bringup` (ESP-IDF). See `src/apps/hardware_bringup/README.md` for the
full API / build reference; this doc is the higher-level explanation and the current status.

## Purpose

Bring up the EV_MOD_ICU-20201 ultrasonic module wired to an ESP32 DevKitV1, on top of the
SonicLib board support package in `src/components/soniclib_esp32_bsp`. The first deliverable is a
**power-on self-test (POST)** that verifies the ESP32 can talk to the sensor over SPI.

## Power-on self-test

The `icu_post` component (`src/components/icu_post/`, `icu_post.{c,h}`) - a reusable,
SonicLib-level module (no direct esp-idf calls beyond `esp_log`) so any app can call `post_run()`
at boot regardless of BSP or wiring changes. All hardware access goes through the `chbsp_*`
callbacks the BSP implements.

| Stage | Call | Pass means |
|---|---|---|
| 1. SPI link | raw `CPU_ID_HI` read + `chdrv_prog_ping()` | Resets the sensor via its `SYS_CTRL` register and reads the `CPU_ID_HI` debug register back over SPI, checking it equals `SHASTA_CPU_ID_HI_VALUE` (`0x2041`). The raw value is logged first (`0x0000` = unpowered/no MISO, `0xFFFF` = MISO floating, stable-wrong = SPI mode/bit-order/timing). Proves wiring, chip-select, **SPI mode 3**, bit order and framing end to end. The headline "ESP32 can talk to the module" check. |
| 2. Program + start | `ch_group_start()` then `ch_sensor_is_connected()` | Firmware downloads to the sensor, frequency locks, RTC calibrates. |
| 3. Sensor identity | `ch_get_part_number` / `ch_get_sensor_id` / `ch_get_fw_version_string` / `ch_get_frequency` / `ch_get_rtc_cal_result` | Programmed sensor reports sane values - part `20201`, operating frequency ~85 kHz (70-95 kHz per DS-000478), non-zero RTC cal. |

`main/post_main.c` is the bring-up harness (`app_main`): `ch_group_init` -> `ch_init` ->
`chbsp_esp32_init` -> `post_run` -> `post_report`, retrying every 5 s on failure. Once the POST
passes it hands off to the rangefinding loop below. Not the production entry point.

## Rangefinding loop

`main/rangefinder_loop.{c,h}` - after the POST passes, `rangefinder_run()` configures the sensor
and streams distance to the console:

1. `ch_meas_init()` (ODR = fop/8) + `icu_gpt_algo_init()` / `icu_gpt_algo_configure()` (1 target,
   ringdown cancel, detection thresholds).
2. Measurement queue: `ch_meas_add_segment_tx()` (burst) -> `ch_meas_add_segment_count()`
   (settle) -> `ch_meas_add_segment_rx()` (listen, done-interrupt on the last segment) ->
   `ch_meas_write_config()`; then `ch_set_algo_config()` / `ch_init_algo()`.
3. `ch_set_max_range(5000)` (5 m one-way, trims the RX sample count),
   `ch_set_freerun_interval(100)` (10 Hz), `ch_set_mode(CH_MODE_FREERUN)`.
4. Data-ready: the BSP's `bsp_int_task` calls the registered callback (`ch_io_int_callback_set`)
   at task level; it does `ch_get_range(dev, CH_RANGE_ECHO_ONE_WAY)` (single sensor, pulse-echo)
   and hands the value to the print loop. `ch_get_range()` returns millimetres x 32, or
   `CH_NO_TARGET`.

The transmit/receive/threshold values in `rangefinder_loop.c` are bring-up starting points and
will likely need tuning on the bench (each `#define` has a note). The 5 m range and 10 Hz
interval are fixed by the requirement.

### Running it

```sh
cd src/apps/hardware_bringup
./build.sh                              # build only (espressif/idf Docker image)
./build.sh -p /dev/ttyUSB0 -b 115200 flash monitor
```

`build.sh` mounts the repo `src/` dir so the shared components under `src/components/`
resolve, passes args through to `idf.py`, and auto-adds the serial device's group
(`--device` / `--group-add`) because the host user is not in the `uucp` group and cannot open
`/dev/ttyUSB0` directly. Console is UART0 (USB serial) at 115200; watch the `bringup` / `POST` /
`rangefinder` log tags. After the POST passes you should see `rangefinder: #N  <dist> mm` lines
(or `no target`) at ~10 Hz.

## Hardware notes captured during bring-up

- **SPI mode 3** (CPOL=1, CPHA=1) is mandatory for the ICU-20201 - DS-000478 Table 1 (pin 2
  SCLK), AN-000357 Table 1 (pin 10 SCLK). The BSP originally set mode 0; fixed to
  `devcfg.mode = 3` in `src/components/soniclib_esp32_bsp/chbsp_esp32_init.c`.
- **INT line roles** (fixed earlier this session): INT1 / GPIO2 = hardware trigger output
  (`CHIRP_SENSOR_TRIG_PIN=1`); INT2 / GPIO4 = data-ready interrupt input
  (`CHIRP_SENSOR_INT_PIN=2`). Both are open-drain at the sensor, held high by external 2.2k
  pull-ups.
- **Power**: module VDD = 1.8V regulated; VDDIO may be 3.3V (direct to the ESP32). The v3 PCB
  reportedly had the 1v8/3v3 rails reversed on the FFC connector - verify against
  `CAD/Rangefinder_test_schematic_v3` before powering the module.
- SPI clock is a conservative 1 MHz (`BSP_SPI_CLOCK_HZ`); datasheet max is 13 MHz.

## SPI vs I2C - verified (2026-09-04)

SonicLib chooses SPI vs I2C **at compile time**, via `#ifdef INCLUDE_SHASTA_SUPPORT ... #elif
defined(INCLUDE_WHITNEY_SUPPORT)` in every transport function (`ch_driver.c`). For this project:

- `INCLUDE_SHASTA_SUPPORT` is defined for **every** translation unit (checked in
  `build/compile_commands.json` - app, BSP, and all `invn-soniclib` sources).
- The linked ELF contains the SPI/Shasta path (`chdrv_prog_ping`, `chdrv_sys_ctrl_read`,
  `chbsp_spi_read`, `icu_gpt_init`) and **zero `chbsp_i2c_*` symbols**. A Whitney/I2C build
  cannot link against this BSP - it has no `chbsp_i2c_init()` and there is no weak stub.
- The `#if !defined(SHASTA) && !defined(WHITNEY)` -> `#define INCLUDE_WHITNEY_SUPPORT` fallback
  in `invn/soniclib/details/chirp_board_config.h` is real but does not trigger, because
  `-DINCLUDE_SHASTA_SUPPORT` is on the command line (set `PUBLIC` in `invn-soniclib`'s
  `CMakeLists.txt`, inherited via `REQUIRES`).

`esp32_bsp_internal.h` and `icu_post.c` now carry an `#error` guard that fails the build if
`INCLUDE_SHASTA_SUPPORT` is missing (or `INCLUDE_WHITNEY_SUPPORT` is also set).

## Status (2026-09-06)

**POST passes on hardware** (all three stages). Free-running rangefinding loop is written and
builds; **not yet run on hardware**. Fixes made along the way:

- FFC pinout in the datasheet was mirrored - the connector had to be rewired 1<->12, 2<->11, ...
- SPI mode 0 -> 3 (DS-000478 / AN-000357).
- INT1/INT2 role swap: INT1 = trigger (`CHIRP_SENSOR_TRIG_PIN=1`), INT2 = data-ready
  (`CHIRP_SENSOR_INT_PIN=2`).
- Interrupt handling: the GPIO ISR now only notifies `bsp_int_task`, which runs `ch_interrupt()`
  at task level (`USE_DEFERRED_INTERRUPT_PROCESSING` removed). Fixes the earlier
  interrupt-watchdog panic from doing SPI in ISR context. See "Interrupt handling" in
  `board_support_package.md`.
- `CH_LOG_MODULE_LEVEL` ERROR -> INFO so SonicLib's discovery/programming steps show on the
  console.

Next: flash and run the rangefinding loop; tune the transmit/receive/threshold values in
`rangefinder_loop.c` against real targets (see `TODO.md`).
