# Ultrasonic Trilateration
The goal of this project is to develop a close-quarters indoor positioning system using ultrasonic trilateration.

This is essentially a small-scale, localized analog of GPS, but using ultrasonic soundwaves instead of radio, and the object being tracked emits the signal rather than the beacons.


## Repo overview
- `docs/` contains all documentation for the project. Theory of operation, results of testing, assembly instructions, etc. `docs/datasheets/` holds component datasheets.
- `src/` contains all source code and firmware:
  - `src/components/` - reusable ESP-IDF components: `invn-soniclib` (vendored SonicLib), `soniclib_esp32_bsp` (the ESP32 board support package for SonicLib), `icu_post` (power-on self-test).
  - `src/apps/` - firmware applications that consume those components (e.g. `hardware_bringup`).
- `CAD/` contains all electronics schematics, PCB files, and any relevant 3D CAD files
- `pics/` contains pictures of the project

See `CLAUDE.md` for a fuller layout and build instructions.


