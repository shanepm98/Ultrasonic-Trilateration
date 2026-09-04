# To - Do

## ESP32 SonicLib BSP (src/soniclib_esp32_bsp) - hardware bring-up

The BSP (`src/main/chbsp_esp32_init.{h,c}`, `src/main/esp32_bsp.c`) is implemented and builds
cleanly (`./build.sh`), but has never run against real hardware. Next steps, in order:

- [ ] Flash to an ESP32 wired to an ICU-20201 (either test rig board) and confirm it boots.
- [ ] Temporarily restore a test harness in `app_main()` (see git history /
      `docs/board_support_package.md` for the sequence: `ch_group_init()` -> `ch_init()` with
      `icu_gpt_init` -> `chbsp_esp32_init()` -> `ch_group_start()`), flash, and check the log for
      `ch_sensor_is_connected()` returning true.
- [ ] If `ch_group_start()` fails or times out, check first:
  - SPI mode (currently assumed CPOL=0/CPHA=0 - mode 0 - against the ICU-20201 datasheet, not
    verified against real hardware yet).
  - SPI clock speed (currently a conservative 1 MHz in `esp32_bsp_internal.h`, `BSP_SPI_CLOCK_HZ`
    - can likely be raised once framing is confirmed working).
  - INT1 wiring/pull-up (GPIO2) - scope it during `ch_group_start()` to confirm the RTC
    calibration pulse and the post-programming interrupt are seen.
- [ ] Once connected, add a real measurement loop (`ch_set_config()` / `ch_set_mode()`,
      `ch_group_trigger()`, read `ch_get_range()` from the INT1 ISR callback) - this BSP only
      covers bring-up, not application logic.
- [ ] Re-remove the test harness from `app_main()` once bring-up is confirmed, per `CLAUDE.md`.
- [ ] Longer-term / only if needed: non-blocking I/Q readout (`chbsp_spi_mem_read_nb`) is currently
      unimplemented (returns error via the `chbsp_dummy.c` stub) - add it if higher-throughput
      non-blocking reads become necessary.


## PCB Design
- [ ] Fix the 1v8/3v3 rails (wired to the FPC connector backwards)
- [ ] Change the terminology used on the solder bridges
- [ ] Add red/green LEDs for indicating operational status (power-on self-test result, runtime errors, etc)
