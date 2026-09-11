# To - Do

## ESP32 SonicLib BSP + echo-mode hardware test

`src/apps/echo_mode_hardware_test/` (renamed from `hardware_bringup`) - POST + free-running
single-sensor rangefinding loop (`main/rangefinder_loop.c`). **Verified running successfully on
real hardware (2026-09-10).**

### Next - tune, then move on to pitch-catch
- [ ] Tune the `RF_*` `#define`s at the top of `rangefinder_loop.c` against real targets:
      thresholds (false targets vs missed targets), `RF_TX_PULSE_US` (far-target echo strength),
      `RF_RX_GAIN_REDUCE` / `RF_RINGDOWN_CANCEL_SAMPLES` (near-field / ringdown false targets).
- [ ] Optional, improves close-range accuracy: `ch_set_init_firmware(&dev, icu_init_init)` before
      `ch_group_start()` + `ch_meas_optimize()` after building the queue (auto-dampens ringdown;
      ~250 ms, run once). `icu_init` is already compiled in.
- [ ] SPI clock is a conservative 1 MHz (`BSP_SPI_CLOCK_HZ` in `esp32_bsp_internal.h`); raise it
      now that framing is confirmed working (datasheet max 13 MHz).
- [ ] Longer-term / only if needed: non-blocking I/Q readout (`chbsp_spi_mem_read_nb`) is
      unimplemented (falls back to the `chbsp_dummy.c` error stub) - add it if higher-throughput
      non-blocking reads become necessary.

## Pitch-catch mode hardware test

`src/apps/pitch_catch_mode_hardware_test/{sender,receiver}` - scaffolded (empty), not yet
implemented. Two sensors: one `CH_MODE_TRIGGERED_TX_RX` (sender), one
`CH_MODE_TRIGGERED_RX_ONLY` (receiver), synchronized via the shared INT1 trigger wire between
boards (see PCB v3 note below). Free-running mode cannot be used for multi-sensor sync - see
AN-000175 §2.4/§2.7.
- [ ] Build out `sender`/`receiver` apps, reusing `src/components/{invn-soniclib,
      soniclib_esp32_bsp,icu_post}` the same way `echo_mode_hardware_test` does.
- [ ] Consider whether `icu_post`'s `post_run()` still applies as-is for a receive-only sensor
      (stage 3 identity checks should be fine; no measurement config assumptions to revisit).

## Wireless synchronization system
In the hardware tests with the V3 revision of the PCB, the sensor trigger lines are hardwired together between the boards.
The next step is to research and develop a wireless system for coordinating all sensors with precision. One promising option
is the ESP-NOW protocol for synchronizing timestamps between boards (e.g, Flooding time synchronization protocol, FTSP),
and then agreeing on a trigger time.

## Sensor tuning/calibration
The currently defined sensor thresholds and parameters are just placeholders for testing the hardware.
To improve the performance of the sensors, they need to be bench-calibrated. An interactive application will
be written to tune these values live, without recompilation, by resetting and reconfiguring the sensor at runtime.
This program should be run on multiple boards at once and get feedback from other boards wirelessy.

## Self-mapping relative coordinate system
The stationary beacons should be able to coordinate with each other and use distance from one another to establish their own local,
relative coordinate system in which to locate the mobile client. More research needed into the feasibility of this.

## PCB Design
- [ ] Change the terminology used on the solder bridges
- [ ] Add red/green LEDs for indicating operational status (power-on self-test result, runtime errors, etc)
