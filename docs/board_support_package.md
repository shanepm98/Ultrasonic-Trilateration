# ESP32 SonicLib Board Support Package

Location: `src/components/soniclib_esp32_bsp` (an ESP-IDF component).

This is a board support package (BSP) that lets [TDK/InvenSense SonicLib
v4](https://github.com/tdk-invn-oss/ultrasonic.soniclib) drive a single ICU-20201 ultrasonic
sensor from an ESP32 (DevkitV1) running ESP-IDF's FreeRTOS. It implements the `chbsp_*` callback
interface SonicLib requires (declared in `invn/soniclib/chirp_bsp.h`) but is not itself an
application - it has no measurement logic and no `app_main()`. It is consumed as a component by
the apps under `src/apps/`.

## Hardware target

Single ICU-20201 sensor wired to the ESP32 over SPI, matching
`CAD/Rangefinder_test_schematic_v3`:

| Signal | ESP32 GPIO | ICU-20201 pin | Role |
|---|---|---|---|
| MOSI | GPIO23 | 3 | SPI |
| MISO | GPIO19 | 4 | SPI |
| SCLK | GPIO18 | 2 | SPI |
| CS   | GPIO5  | 5 | SPI, manual (software) chip-select |
| INT1 | GPIO2  | 6 | Hardware trigger (output, external 2.2k pull-up to 3.3V) |
| INT2 | GPIO4  | 7 | Data-ready interrupt (input, external 2.2k pull-up to 3.3V) |

No I2C, RESET_N, or PROG lines are used - ICU/Shasta-generation sensors don't have them. Pin
numbers and SPI parameters (host, clock speed, DMA scratch buffer size) live in
`esp32_bsp_internal.h` if the wiring ever changes.

SPI is **mode 3** (CPOL=1, CPHA=1) - required by the ICU-20201, DS-000478 Table 1 (pin 2 SCLK)
and AN-000357 Table 1 (pin 10 SCLK). Set in `chbsp_esp32_init.c` (`devcfg.mode = 3`). Clock is a
conservative 1 MHz (`BSP_SPI_CLOCK_HZ`); datasheet max is 13 MHz. INT1/INT2 are open-drain on the
sensor, held high by the external 2.2k pull-ups; fine for a single board, but relevant if the INT1
trigger line is ever wired between two boards for pitch-catch (see `CAD/Rangefinder_test_schematic_v3`).

Wiring above matches `CAD/Rangefinder_test_schematic_v3` (SPI unchanged from the earlier revision
the BSP was first written against).

## File layout

The BSP component is `src/components/soniclib_esp32_bsp/`:

- `include/chbsp_esp32_init.h` - the one public header: `chbsp_esp32_init()`. See "Usage" below.
- `esp32_bsp_internal.h` - pin assignments and hardware handles shared between the two `.c`
  files. Private (`PRIV_INCLUDE_DIRS`), not part of the public interface. Also carries the
  compile-time `#error` guard requiring `INCLUDE_SHASTA_SUPPORT`.
- `chbsp_esp32_init.c` - one-time GPIO/SPI/ISR/event-group/task setup.
- `esp32_bsp.c` - the `chbsp_*` implementations, the GPIO ISR handler for INT2 (data-ready), and
  `bsp_int_task` (see "Interrupt handling" below). Includes the vendored
  `<invn/soniclib/chirp_bsp.h>` directly (no local copy).

The vendored SonicLib lives in a separate component, `src/components/invn-soniclib/` (ICU/Shasta
support + GPT rangefinding firmware only). Its `CMakeLists.txt` sets the board configuration
(`CHIRP_MAX_NUM_SENSORS`, `CHIRP_NUM_BUSES`, `CHIRP_SENSOR_INT_PIN`, `CHIRP_SENSOR_TRIG_PIN`,
`MAX_PROG_XFER_SIZE`, `INCLUDE_SHASTA_SUPPORT`, `CH_LOG_MODULE_LEVEL`) via `PUBLIC` compile
definitions rather than a `chirp_board_config.h` file, so there's no circular dependency between
the library and the BSP.

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

To use the BSP from another app, point that app's `EXTRA_COMPONENT_DIRS` at `src/components/`
(see `src/apps/hardware_bringup/CMakeLists.txt`) and add `soniclib_esp32_bsp` to your component's
`REQUIRES`. It brings `invn-soniclib` in transitively.

## What's implemented vs. not

Implemented for real: INT2 (data-ready interrupt) direction/level/enable control, INT1 (hardware
trigger) direction/level control, manual SPI chip-select plus blocking SPI read/write (routed
through a DMA-capable scratch buffer), microsecond/millisecond delay, millisecond timestamp, and
the event-wait/notify primitives SonicLib uses internally during `ch_group_start()` (backed by a
FreeRTOS event group).

Deliberately not implemented (the sensor/board don't need them, or SonicLib's ICU/Shasta code
path never calls them - see `invn/soniclib/chirp_bsp.h` for which functions are
Whitney/CH101/CH201-only):
I2C (any of it), sensor RESET_N/PROG control, debug indicator pins (none wired on this board),
and non-blocking SPI I/Q readout (`chbsp_spi_mem_read_nb` - can be added later if needed). These
fall back to the harmless no-op weak stubs in SonicLib's own `chbsp_dummy.c`.

## Interrupt handling

SonicLib's `ch_interrupt()` (called on each data-ready) runs `chdrv_int_callback_deferred()`,
which does blocking SPI reads (interrupt-source register, measurement metadata) and calls the
application's data-ready callback. That cannot run in ISR context - `spi_device_transmit()` would
deadlock (the earlier bring-up hit an "Interrupt wdt timeout ... running in ISR context" panic).

So the BSP splits it: `bsp_int2_isr_handler` (the GPIO ISR) does nothing but
`vTaskNotifyGiveFromISR(bsp_int_task_handle)`. A dedicated high-priority task, `bsp_int_task`,
takes that notification and calls `ch_interrupt(bsp_grp_ptr, 0)` at task level, then re-arms the
GPIO interrupt (`chdrv_int_callback()` disables it while it toggles the INT pin). This path
serves both `ch_group_start()` and normal measurements, so `USE_DEFERRED_INTERRUPT_PROCESSING` is
**not** defined. `chbsp_event_notify()` is written ISR-safe anyway (`xPortInIsrContext()` check).

An application registers its data-ready callback with `ch_io_int_callback_set()`; SonicLib invokes
it from `bsp_int_task` context, so it may call `ch_get_range()` etc. directly (see
`src/apps/hardware_bringup/main/rangefinder_loop.c`).

One deliberate deviation from the "obvious" FreeRTOS approach: `chbsp_delay_ms()` busy-waits
(`esp_rom_delay_us()`) rather than calling `vTaskDelay()`. `CONFIG_FREERTOS_HZ=100` gives only
10ms tick resolution, which is too coarse for the RTC calibration pulse this function times
during `ch_group_start()` - `chirp_bsp.h` notes that pulse's accuracy directly affects range
accuracy. This only blocks the calling task for tens to hundreds of ms during one-time startup
calibration, not during normal measurement.

## Building

The BSP is a component, not a standalone project - it is built as part of whichever app pulls it
in. `cd src/apps/hardware_bringup && ./build.sh` exercises it.
