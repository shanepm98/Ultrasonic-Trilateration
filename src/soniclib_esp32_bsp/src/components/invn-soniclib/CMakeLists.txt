idf_component_register(
    SRCS "invn/soniclib/chbsp_dummy.c"
         "invn/soniclib/ch_api.c"
         "invn/soniclib/ch_common.c"
         "invn/soniclib/ch_driver.c"
         "invn/soniclib/ch_math_utils.c"
         "invn/soniclib/ch_log.c"
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
    CHIRP_MAX_NUM_SENSORS=1
    CHIRP_NUM_BUSES=1
    CHIRP_SENSOR_INT_PIN=1
    CHIRP_SENSOR_TRIG_PIN=2
    MAX_PROG_XFER_SIZE=2048
    CH_LOG_MODULE_LEVEL=4
)
