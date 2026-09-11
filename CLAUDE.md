# Project guide

Close-quarters indoor positioning via ultrasonic trilateration. The tracked object emits;
fixed beacons listen. Sensor: TDK/InvenSense ICU-20201 (Shasta generation) driven by an ESP32
DevKitV1 over SPI, using TDK's SonicLib.

## Repo layout

- `src/components/` - reusable ESP-IDF components, shared by every app:
  - `invn-soniclib/` - vendored SonicLib (ICU/Shasta + GPT rangefinding fw). Single copy.
  - `soniclib_esp32_bsp/` - the `chbsp_*` board support package for one ICU-20201 over SPI on
    the ESP32. Public header in `include/`; `esp32_bsp_internal.h` is private.
  - `icu_post/` - reusable power-on self-test (`post_run()` / `post_report()`). SonicLib-level
    only, no esp-idf/BSP calls beyond `esp_log`. Any app can call it at boot.
- `src/apps/<name>/` - thin ESP-IDF projects. Each has `CMakeLists.txt` (sets
  `EXTRA_COMPONENT_DIRS` to `../../components`), `main/`, `sdkconfig.defaults`, `build.sh`.
  - `echo_mode_hardware_test/` - flashes the POST + free-running echo-mode (pulse-echo, single
    sensor) rangefinding loop and reports over serial. Verified working on hardware. Not
    production firmware.
  - `pitch_catch_mode_hardware_test/{sender,receiver}` - two-sensor pitch-catch bring-up
    (scaffolded, not yet implemented).
- `docs/` - project documentation (theory, bring-up notes, BSP reference). `docs/datasheets/`
  holds the PDFs; `docs/vendor/` holds excerpts from TDK docs.
- `CAD/` - schematics, PCB, gerbers. `pics/` - photos and rendered schematics.

## Building

Each app has a `build.sh` that runs `idf.py` inside the `espressif/idf` Docker image, mounting
`src/` so the shared components resolve. `./build.sh` builds; `./build.sh flash monitor`
flashes (needs the board on `$PORT`, default `/dev/ttyUSB0`). The host user is not in the
`uucp` group, so serial always goes through the container (`build.sh` handles `--device` /
`--group-add`).

## Code style

- BSP functions are thin wrappers over esp-idf / esp-idf FreeRTOS calls.
- If a function depends on prior init, say so in a comment and keep the init in a separate file
  (`chbsp_esp32_init.c`).
- Match the surrounding file's style (tabs, comment density, naming).

## Wrap-up (end of session / task)

- Update `docs/` (explanation + usage) when behavior changes.
- Update `TODO.md` with concrete next steps.
- Add a dated entry to `journal.md` summarizing progress / setbacks.
