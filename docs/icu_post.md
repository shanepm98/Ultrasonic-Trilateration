# icu_post — ICU-20201 power-on self-test

Component: `src/components/icu_post/` (`icu_post.{c,h}`). See its `README.md` for the short
API summary and how to wire it into a new app's `CMakeLists.txt`; this page is the fuller
explanation of what each stage does, how to read a failure, and how the module fits the rest of
the codebase.

## Purpose

A fast, reusable "is the sensor alive and talking?" check that firmware can run at boot before
doing anything else. It is deliberately **SonicLib-level only** — no direct esp-idf or ESP32 BSP
calls beyond `esp_log` — so it stays valid if the board support package or wiring changes. All
hardware access goes through the `chbsp_*` callbacks that whichever BSP the app links against
already implements (currently `soniclib_esp32_bsp`, see `docs/board_support_package.md`). Any app
in `src/apps/` can call `post_run()` without depending on ESP32-specific headers.

## Usage contract

`post_run()` operates on an already-initialised sensor. The caller is responsible for, in order:

1. `ch_group_init(&grp, CHIRP_MAX_NUM_SENSORS, CHIRP_NUM_BUSES, CHIRP_RTC_CAL_PULSE_MS)`
2. `ch_init(&dev, &grp, 0, icu_gpt_init)`
3. The BSP's hardware init (e.g. `chbsp_esp32_init(&grp)`) — sets up GPIO/SPI/ISR so the sensor
   is reachable. This step is **not** `post_run()`'s job; see `docs/board_support_package.md` for
   why it must happen between `ch_init()` and any call that reaches the sensor.

```c
post_config_t cfg;
post_config_default(&cfg);
post_result_t res;
bool ok = post_run(&grp, &dev, &cfg, &res);
post_report(&res);
```

`cfg` and `res` may both be `NULL` if only the boolean pass/fail matters. `post_report()` is
callable independently any time after `post_run()`, to re-dump the last result from application
code.

## Stages

Stages run in order; `cfg.stop_on_fail` (default `true`) skips remaining stages after the first
hard failure so a bad SPI link doesn't cascade into confusing downstream errors.

### 1. SPI link — `chdrv_prog_ping()`

Resets the sensor through its `SYS_CTRL` register (asserts debug mode + reset, then releases
reset) and reads back the `CPU_ID_HI` debug register over SPI, checking it equals
`SHASTA_CPU_ID_HI_VALUE` (`0x2041`). Before calling `chdrv_prog_ping()` itself, `icu_post` does
its own raw read of the same register and logs the value — this raw value is what pins down the
failure mode when the ping fails:

| Raw `CPU_ID_HI` | Meaning |
|---|---|
| `0x0000` | MISO reads all-0 — sensor unpowered (check the 1.8V VDD rail), no SCLK, or MISO not wired |
| `0xFFFF` | MISO reads all-1 — MISO floating / not driven; check the MISO line, CS (GPIO5), FFC seating |
| any other wrong value | SPI mode wrong (must be **mode 3**, CPOL=1 CPHA=1), bit order wrong, clock too fast, or a level-shifter problem |
| a BSP SPI transfer error | The `chbsp_spi_*` callback layer itself failed — an SPI bus problem in the BSP, not the sensor |

A pass here proves wiring, chip-select, SPI mode, bit order and framing are all correct end to
end — it is the "can the ESP32 (or whichever host) talk to the module over SPI" headline check,
also exposed as `post_result_t.spi_link_ok`.

### 2. Program + start — `ch_group_start()` + `ch_sensor_is_connected()`

Runs SonicLib's full discovery: firmware download to the sensor, frequency lock, and RTC
calibration, then confirms `ch_sensor_is_connected()`. This is the expensive stage — it is
skipped entirely if `cfg.run_group_start` is `false`, leaving stage 1 as a fast SPI-only smoke
test (useful for a quick wiring check without the cost of a full firmware download).

### 3. Sensor identity — parameter read-back and sanity check

Reads back part number, sensor ID (lot + serial from OTP, or `"NOTPROG"` if OTP is blank),
firmware version string, operating frequency, and RTC calibration result, then checks:

- part number matches `cfg.expected_part_number` (default `ICU20201_PART_NUMBER` = 20201; set to
  `0` to disable the check)
- operating frequency is non-zero (a hard failure — a zero reading means the frequency-lock step
  didn't actually work)
- RTC calibration result is non-zero
- (soft check, warning only) operating frequency falls within 65–100 kHz — the ICU-20201's
  datasheet (DS-000478) specifies 70–95 kHz nominal 85 kHz across process/temperature; a reading
  outside that band is suspicious but not automatically a failure the way a zero reading is

## Result structures

`post_result_t` (see `include/icu_post.h` for the authoritative field list):

- `all_pass` — every *attempted* stage passed (a skipped stage doesn't count against this)
- `spi_link_ok` — headline stage-1 result
- `stage[POST_STAGE_COUNT]` — per-stage `post_stage_result_t { run, pass, code, detail }`
- `part_number`, `sensor_id`, `fw_version`, `op_frequency_hz`, `rtc_cal_result` — filled in once
  stage 3 runs

`post_config_t`:

- `run_group_start` (default `true`) — run stages 2–3; `false` restricts POST to the SPI-link
  smoke test
- `stop_on_fail` (default `true`) — abort remaining stages after the first hard failure
- `expected_part_number` (default `ICU20201_PART_NUMBER`) — `0` disables the stage-3 part-number
  check

## Why SonicLib-level only

The module intentionally avoids any esp-idf or ESP32-specific header beyond `esp_log`. Everything
it does — `chdrv_prog_ping()`, `ch_group_start()`, the `ch_get_*()` read-backs — goes through
SonicLib's own API, which in turn reaches hardware only via the `chbsp_*` callback interface a BSP
implements. That means `icu_post` needs no changes if the BSP is swapped (a different MCU, a
different wiring layout) or if a second/third sensor is added — it only assumes one already-`ch_init()`'d
sensor `ch_dev_t` was handed to it, which is the same assumption every SonicLib application makes.

The one exception is `#include "esp_log.h"` (`ESP_LOGI`/`ESP_LOGW`/`ESP_LOGE`) for console output;
this is esp-idf-specific but is the same logging facility every app in this repo already uses, so
it was judged not worth abstracting away.

The component also carries a compile-time guard (`#if !defined(INCLUDE_SHASTA_SUPPORT) ||
defined(INCLUDE_WHITNEY_SUPPORT)`) that fails the build if SonicLib wasn't configured for the
Shasta/SPI path the ICU-20201 needs — see "SPI vs I2C" in `docs/hardware_bringup.md` for why that
distinction matters.

## Example / reference usage

`src/apps/echo_mode_hardware_test/main/post_main.c` is the working reference: it runs
`post_run()` in a retry loop (every 5 s on failure) so the FFC/wiring can be re-seated and watched
for recovery, then hands off to the rangefinding loop once POST passes. See
`docs/hardware_bringup.md` for the bring-up narrative and current hardware status.

## Including in a new app

See the "Adding this component to a new app" section of `src/components/icu_post/README.md` for
the `CMakeLists.txt` / `REQUIRES` wiring.
