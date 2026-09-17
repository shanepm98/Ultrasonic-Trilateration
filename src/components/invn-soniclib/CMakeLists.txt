idf_component_register(
    SRCS "invn/soniclib/chbsp_dummy.c"
         "invn/soniclib/ch_api.c"
         "invn/soniclib/ch_common.c"
         "invn/soniclib/ch_driver.c"
         "invn/soniclib/ch_math_utils.c"
         "invn/soniclib/ch_log.c"
         "invn/soniclib/ch_rangefinder.c"
         "invn/soniclib/extra/ch_extra_display_utils.c"
         "invn/soniclib/sensor_fw/icu_gpt/icu_gpt.c"
         "invn/soniclib/sensor_fw/icu_gpt/icu_gpt_fw.c"
         "invn/soniclib/sensor_fw/icu_init/icu_init.c"
         "invn/soniclib/sensor_fw/icu_init/icu_init_fw.c"
    INCLUDE_DIRS "."
)

# soniclib.h normally requires a chirp_board_config.h on the include path (INVN_SONICLIB_INTERNAL_BOARD_CONFIG
# undefined). Defining INVN_SONICLIB_INTERNAL_BOARD_CONFIG instead makes it use its own internal default config
# header and take these values from compile definitions, which avoids a circular component dependency between
# this library and the BSP (see plan doc). PUBLIC so consumers of this component (the BSP) inherit them too.
target_compile_definitions(${COMPONENT_LIB} PUBLIC
    INVN_SONICLIB_INTERNAL_BOARD_CONFIG
    INCLUDE_SHASTA_SUPPORT
    # Interrupts: the BSP's GPIO ISR does NOT call ch_interrupt() - it notifies a dedicated
    # FreeRTOS task (bsp_int_task in esp32_bsp.c) which calls ch_interrupt() at task level, so the
    # SPI-heavy chdrv_int_callback_deferred() runs where blocking spi_device_transmit() is legal.
    # USE_DEFERRED_INTERRUPT_PROCESSING is therefore intentionally left undefined (it only matters
    # when the ISR itself calls ch_interrupt()).
    CHIRP_MAX_NUM_SENSORS=1
    CHIRP_NUM_BUSES=1
    CHIRP_SENSOR_INT_PIN=2
    CHIRP_SENSOR_TRIG_PIN=1
    MAX_PROG_XFER_SIZE=2048
    # SonicLib internal log verbosity: 0=TRACE 1=DEBUG 2=INFO 3=WARNING 4=ERROR 6=DISABLE.
    # INFO during bring-up so the discovery / programming / RTC-cal steps show on the console
    # (e.g. "Device 0 found on bus", "Ping failed, device 0 not found", "Sensor count: N").
    CH_LOG_MODULE_LEVEL=2
)
