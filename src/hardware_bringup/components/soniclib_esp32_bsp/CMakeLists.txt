# Thin wrapper component: reuse the canonical ESP32 SonicLib BSP sources in place
# (src/soniclib_esp32_bsp/src/main) without copying them into this project, so there is a
# single source of truth for the chbsp_* implementation.
#
# The BSP's own project registers those files as a component literally named "main", which
# would collide with this project's "main"; registering them here under a different component
# name avoids that.

set(_bsp_main "${CMAKE_CURRENT_LIST_DIR}/../../../soniclib_esp32_bsp/src/main")

idf_component_register(
    SRCS "${_bsp_main}/esp32_bsp.c"
         "${_bsp_main}/chbsp_esp32_init.c"
    INCLUDE_DIRS "${_bsp_main}"
    REQUIRES invn-soniclib esp_driver_gpio esp_driver_spi esp_timer esp_rom log
)
