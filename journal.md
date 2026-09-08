# Project Journal
This doc is for briefly summarizing daily progress/thoughts/setbacks for future reference

## 9-6-2026
- Housekeeping: restructured the repo into `src/components/` (invn-soniclib, soniclib_esp32_bsp, icu_post) + `src/apps/hardware_bringup/`; dropped ~1400 committed build artifacts from git. Docs/paths updated. Builds verified.
- Wrote the free-running rangefinding loop (`src/apps/hardware_bringup/main/rangefinder_loop.c`): builds a measurement queue, configures icu_gpt algo + thresholds, `ch_set_max_range(5000)`, `ch_set_freerun_interval(100)`, `ch_set_mode(CH_MODE_FREERUN)`, prints `ch_get_range()` from the data-ready callback. Builds clean; not yet run on hardware. TX/RX/threshold values are starting guesses to tune on the bench.
- BSP interrupt refactor: GPIO ISR now only notifies a dedicated task (`bsp_int_task`) which calls `ch_interrupt()` at task level; removed `USE_DEFERRED_INTERRUPT_PROCESSING`. This is the proper fix for the earlier "SPI in ISR context" watchdog panic and makes the runtime data-ready callback work.

## 9-5-2026
- Fixed FFC cable wiring problem (all pins mirrored- 12 should be 1, 11 should be 2, 10 should be 3, etc) and got POST running and passing successfully
- The BSP (`src/soniclib_esp32_bsp`) and the power-on self-test (`src/hardware_bringup`) build
cleanly. USB cable issue (2026-09-03) resolved. FFC pinout in the datasheet was wrong - pins had to be
reversed (1<->12, 2<->11, ...); after rewiring, stage 1 (SPI link) PASSES (CPU ID 0x2041, device
found, firmware programmed). See `docs/hardware_bringup.md`.
- Then hit an `Interrupt wdt timeout ... running in ISR context` during firmware start. Cause: the
BSP GPIO ISR calls `ch_interrupt()`, which (without `USE_DEFERRED_INTERRUPT_PROCESSING`) ran the
SPI-heavy `chdrv_int_callback_deferred()` inline in ISR context -> blocking `spi_device_transmit()`
deadlock. Fixed by adding `USE_DEFERRED_INTERRUPT_PROCESSING` to `invn-soniclib/CMakeLists.txt`
(2026-09-05). Re-flash and continue from stage 2.

## 9-3-2026
- Started hardware bring-up. Had Claude write a reusable power-on self-test (`src/hardware_bringup`) that checks SPI comms to the ICU-20201 via SonicLib `chdrv_prog_ping()`, then `ch_group_start()`, then an identity read-back. Builds clean. Also fixed the BSP SPI mode (0 -> 3, per DS-000478 / AN-000357) and swapped the INT1/INT2 roles (INT1 = trigger, INT2 = data-ready).
- Setback: could not flash the ESP32 - USB serial link is unreliable (esptool reaches the ROM but flash writes abort mid-transfer, boot log comes back with bytes duplicated ~80x). Board is fine (boots factory ESP-AT off 4MB flash). Almost certainly a bad USB cable; no spare on hand. Resuming once a cable is available - see `TODO.md` / `docs/hardware_bringup.md`.

## 7-22-2026
- Designed a circuit that can function as either master or slave beacon, designed the PCB, and ordered that + parts. Waiting on board and parts. Next step is to bring up the hardware and do a hardware test of the board support package

## 7-8-2026
- Had Claude assist in writing the SonicLib board support package for the ESP32 Devkit V1. This is the intermediate layer between the high-level SonicLib business logic and the low-level ESP32 hardware, basically the driver to control the pins and timers. It is NOT tested in hardware yet, although it is verified to build successfully. Also had claude write `docs/board_support_package.md` and update `TODO.md` with next steps for testing the BSP

## 7-5-2026
- Revised the test rig to use the newer ICU-20201 ultrasonic chip instead of the CH201. Redesigned the test rig so that each sensor has its own ESP32 for the sake of convenience and not running a ton of long wires

## 6-24-2026
- Finished and uploaded schematics for a CH201 test rig. Two CH201's operate in pitch-catch mode, controlled by an ESP32. Hardware has yet to be built and I still need to write the ESP32 firmware

## 6-2-2026
- Created this GitHub account

