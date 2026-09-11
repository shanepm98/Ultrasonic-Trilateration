# icu_post

Reusable power-on self-test (POST) for a single ICU-20201 ultrasonic sensor reached over SPI via
TDK/InvenSense SonicLib. SonicLib-level only — no direct esp-idf/BSP calls beyond `esp_log` — so
any app can call it at boot regardless of which board support package or wiring is in use. See
`docs/icu_post.md` for the full explanation of each stage and how to interpret a failure.

## Stages

| # | Stage | Call | A pass proves |
|---|---|---|---|
| 1 | SPI link | `chdrv_prog_ping()` (plus a raw `CPU_ID_HI` debug-register read for diagnostics) | Wiring, chip-select, SPI mode, bit order and framing are all correct end to end |
| 2 | Program + start | `ch_group_start()` then `ch_sensor_is_connected()` | Firmware downloaded, frequency locked, RTC calibrated |
| 3 | Sensor identity | reads part number / serial / fw version / operating frequency / RTC cal result | The programmed sensor reports sane values (part `20201`, f_op ~85 kHz) |

## API

```c
#include "icu_post.h"

post_config_t cfg;
post_config_default(&cfg);      // full test, stop-on-fail, expect ICU20201_PART_NUMBER
// cfg.run_group_start = false;    // SPI-link-only smoke test
// cfg.stop_on_fail    = false;    // run every stage regardless of earlier failures
// cfg.expected_part_number = 0;   // disable the part-number check

post_result_t res;
bool ok = post_run(&grp, &dev, &cfg, &res);   // grp/dev already ch_group_init()/ch_init()'d
post_report(&res);                            // dump the result table (ESP_LOG, tag "POST")
```

`post_run()` does **not** call `chbsp_esp32_init()` — the caller must already have run
`ch_group_init()`, `ch_init()`, and the BSP's hardware init before calling it. `cfg` and `out` may
both be `NULL` if only the `bool` return value matters. See `include/icu_post.h` for the full
`post_result_t` / `post_stage_result_t` field docs.

## Adding this component to a new app

1. Point the app's `CMakeLists.txt` at the shared components directory:
   ```cmake
   set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/../../components")
   ```
2. Add `icu_post` to the consuming source file's component `REQUIRES` (it pulls in
   `invn-soniclib` transitively):
   ```cmake
   idf_component_register(SRCS "main.c"
                           REQUIRES soniclib_esp32_bsp icu_post invn-soniclib log)
   ```
3. Call it after `ch_group_init()` / `ch_init()` / the BSP's init, before doing anything else with
   the sensor:
   ```c
   ch_group_init(&grp, CHIRP_MAX_NUM_SENSORS, CHIRP_NUM_BUSES, CHIRP_RTC_CAL_PULSE_MS);
   ch_init(&dev, &grp, 0, icu_gpt_init);
   chbsp_esp32_init(&grp);

   post_config_t cfg;
   post_config_default(&cfg);
   post_result_t res;
   bool ok = post_run(&grp, &dev, &cfg, &res);
   post_report(&res);
   ```

See `src/apps/echo_mode_hardware_test/main/post_main.c` for a working example (retries on
failure) and `docs/icu_post.md` for the full stage-by-stage reference.
