# To - Do

## ESP32 SonicLib BSP + hardware bring-up

POST passes on hardware. Free-running rangefinding loop (`src/apps/hardware_bringup/main/
rangefinder_loop.c`) is written + builds; not yet run on hardware.

### Resume here - run + tune the rangefinding loop
- [ ] Flash and monitor: `cd src/apps/hardware_bringup && ./build.sh -p /dev/ttyUSB0 -b 115200 flash monitor`.
      After the POST you should get `rangefinder: #N <dist> mm` (or `no target`) at ~10 Hz.
- [ ] Tune the `RF_*` `#define`s at the top of `rangefinder_loop.c` against real targets:
      thresholds (false targets vs missed targets), `RF_TX_PULSE_US` (far-target echo strength),
      `RF_RX_GAIN_REDUCE` / `RF_RINGDOWN_CANCEL_SAMPLES` (near-field / ringdown false targets).
- [ ] If `rangefinder` logs "no data-ready ...": scope INT2 (GPIO4) for the per-measurement
      data-ready pulse; check `bsp_int_task` is being notified.
- [ ] Optional, improves close-range accuracy: `ch_set_init_firmware(&dev, icu_init_init)` before
      `ch_group_start()` + `ch_meas_optimize()` after building the queue (auto-dampens ringdown;
      ~250 ms, run once). `icu_init` is already compiled in.
- [ ] SPI clock is a conservative 1 MHz (`BSP_SPI_CLOCK_HZ` in `esp32_bsp_internal.h`); raise it
      once framing is confirmed (datasheet max 13 MHz).
- [ ] Longer-term / only if needed: non-blocking I/Q readout (`chbsp_spi_mem_read_nb`) is
      unimplemented (falls back to the `chbsp_dummy.c` error stub) - add it if higher-throughput
      non-blocking reads become necessary.


## PCB Design
- [ ] Change the terminology used on the solder bridges
- [ ] Add red/green LEDs for indicating operational status (power-on self-test result, runtime errors, etc)
