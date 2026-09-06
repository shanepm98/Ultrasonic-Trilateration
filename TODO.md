# To - Do

## ESP32 SonicLib BSP + hardware bring-up
### Resume here - stage 2 (`ch_group_start`) after the ISR-context fix
- [ ] SPI clock is a conservative 1 MHz (`BSP_SPI_CLOCK_HZ` in `esp32_bsp_internal.h`); raise it
      once framing is confirmed (datasheet max 13 MHz).
- [ ] Optional, improves TX calibration: `ch_set_init_firmware(&dev, icu_init_init)` before
      `ch_group_start()` (the `icu_init` firmware is already compiled in). Not required - the main
      `icu_gpt` firmware self-inits when no init firmware is set.
- [ ] Once the POST passes, build the real measurement loop (`ch_set_config()` / `ch_set_mode()`,
      `ch_group_trigger()`, read `ch_get_range()` from the INT2 ISR callback). Call `post_run()`
      at boot in the real app. The BSP + POST only cover bring-up, not application logic.
- [ ] Longer-term / only if needed: non-blocking I/Q readout (`chbsp_spi_mem_read_nb`) is
      unimplemented (falls back to the `chbsp_dummy.c` error stub) - add it if higher-throughput
      non-blocking reads become necessary.


## PCB Design
- [ ] Change the terminology used on the solder bridges
- [ ] Add red/green LEDs for indicating operational status (power-on self-test result, runtime errors, etc)
