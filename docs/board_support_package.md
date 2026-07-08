# ESP32 SonicLib Board Support Package

Location: `src/soniclib_esp32_bsp`

This is a board support package (BSP) that lets [TDK/InvenSense SonicLib
v4](https://github.com/tdk-invn-oss/ultrasonic.soniclib) drive a single ICU-20201 ultrasonic
sensor from an ESP32 (DevkitV1) running ESP-IDF's FreeRTOS. It implements the `chbsp_*` callback
interface SonicLib requires (defined in `chirp_bsp.h`) but is not itself an application - it has
no measurement logic and, in its finished state, no `app_main()`. It's meant to be consumed as an
ESP-IDF component by an application project (e.g. the rangefinder test rig firmware).

## Hardware target

Single ICU-20201 sensor wired to the ESP32 over SPI, matching
`rangefinder_client.kicad_sch` / `rangefinder_master_beacon.kicad_sch` (identical wiring on both
boards):

| Signal | ESP32 GPIO | ICU-20201 pin | Role |
|---|---|---|---|
| MOSI | GPIO23 | 3 | SPI |
| MISO | GPIO19 | 4 | SPI |
| SCLK | GPIO18 | 2 | SPI |
| CS   | GPIO5  | 5 | SPI, manual (software) chip-select |
| INT1 | GPIO2  | 6 | Data-ready interrupt (input, external 2.2k pull-up to 3.3V) |
| INT2 | GPIO4  | 7 | Hardware trigger (output, external 2.2k pull-up to 3.3V) |

No I2C, RESET_N, or PROG lines are used - ICU/Shasta-generation sensors don't have them. Pin
numbers and SPI parameters (host, clock speed, DMA scratch buffer size) live in
`src/main/esp32_bsp_internal.h` if the wiring ever changes.

## File layout

- `src/components/invn-soniclib/` - vendored SonicLib source (ICU/Shasta support + GPT
  rangefinding firmware only), copied from the upstream repo. Its `CMakeLists.txt` sets the board
  configuration (`CHIRP_MAX_NUM_SENSORS`, `CHIRP_NUM_BUSES`, `CHIRP_SENSOR_INT_PIN`,
  `CHIRP_SENSOR_TRIG_PIN`, `MAX_PROG_XFER_SIZE`) via `PUBLIC` compile definitions rather than a
  `chirp_board_config.h` file, so there's no circular dependency between this library component
  and the BSP component.
- `src/main/chirp_bsp.h` - vendor-supplied interface definition (unmodified).
- `src/main/esp32_bsp_internal.h` - pin assignments and the hardware handles shared between the
  two files below. Not part of the public interface.
- `src/main/chbsp_esp32_init.{h,c}` - one-time hardware setup. See "Usage" below.
- `src/main/esp32_bsp.c` - the `chbsp_*` function implementations themselves, plus the GPIO ISR
  handler for INT1.

## Usage

An application that wants to use this BSP must call `chbsp_esp32_init()` **after**
`ch_group_init()`/`ch_init()` (so the `ch_group_t`/`ch_dev_t` exist) and **before**
`ch_group_start()` (so the SPI/GPIO/interrupt hardware is ready when SonicLib starts probing the
sensor):

```c
#include <invn/soniclib/soniclib.h>
#include <invn/soniclib/sensor_fw/icu_gpt/icu_gpt.h>
#include "chbsp_esp32_init.h"

static ch_group_t grp;
static ch_dev_t dev;

void app_main(void) {
    ch_group_init(&grp, CHIRP_MAX_NUM_SENSORS, CHIRP_NUM_BUSES, CHIRP_RTC_CAL_PULSE_MS);
    ch_init(&dev, &grp, 0, icu_gpt_init);

    chbsp_esp32_init(&grp);   /* sets up GPIO/SPI/ISR/event group - must run before ch_group_start() */

    ch_group_start(&grp);
    /* ch_sensor_is_connected(&dev) should now be true; proceed to ch_set_config()/ch_set_mode()/
       ch_group_trigger() etc. as normal SonicLib application code. */
}
```

To use the BSP from another ESP-IDF project, add both `src/main` (or copy its files into your
own component) and `src/components/invn-soniclib` as components, with your component requiring
`invn-soniclib` the same way `src/main/CMakeLists.txt` does here.

## What's implemented vs. not

Implemented for real: INT1 (data-ready interrupt) direction/level/enable control, INT2 (hardware
trigger) direction/level control, manual SPI chip-select plus blocking SPI read/write (routed
through a DMA-capable scratch buffer), microsecond/millisecond delay, millisecond timestamp, and
the event-wait/notify primitives SonicLib uses internally during `ch_group_start()` (backed by a
FreeRTOS event group; `chbsp_event_notify()` runs in ISR context via
`xEventGroupSetBitsFromISR()`).

Deliberately not implemented (the sensor/board don't need them, or SonicLib's ICU/Shasta code
path never calls them - see `chirp_bsp.h` for which functions are Whitney/CH101/CH201-only):
I2C (any of it), sensor RESET_N/PROG control, debug indicator pins (none wired on this board),
and non-blocking SPI I/Q readout (`chbsp_spi_mem_read_nb` - can be added later if needed). These
fall back to the harmless no-op weak stubs in SonicLib's own `chbsp_dummy.c`.

One deliberate deviation from the "obvious" FreeRTOS approach: `chbsp_delay_ms()` busy-waits
(`esp_rom_delay_us()`) rather than calling `vTaskDelay()`. `CONFIG_FREERTOS_HZ=100` gives only
10ms tick resolution, which is too coarse for the RTC calibration pulse this function times
during `ch_group_start()` - `chirp_bsp.h` notes that pulse's accuracy directly affects range
accuracy. This only blocks the calling task for tens to hundreds of ms during one-time startup
calibration, not during normal measurement.

## Building

```sh
./build.sh   # runs idf.py build inside the espressif/idf Docker image
```

Note: as delivered, `src/main` has no `app_main()` (removed once the BSP itself was verified to
compile and link cleanly, per this project's `CLAUDE.md`), so `./build.sh` will succeed through
compiling and archiving every component but fail at the final link step with `undefined reference
to app_main`. That's expected for a standalone build of this BSP - see `TODO.md` for the temporary
harness to restore when testing against real hardware.
