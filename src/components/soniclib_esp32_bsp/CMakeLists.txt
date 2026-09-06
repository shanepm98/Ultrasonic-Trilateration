# ESP32 (esp-idf) board support package for TDK/InvenSense SonicLib - the chbsp_* callback
# implementation for a single ICU-20201 sensor over SPI. Consumed as a component by the apps
# under src/apps/. See docs/board_support_package.md.

idf_component_register(
    SRCS "esp32_bsp.c" "chbsp_esp32_init.c"
    INCLUDE_DIRS "include"                 # public: chbsp_esp32_init.h
    PRIV_INCLUDE_DIRS "."                  # private: esp32_bsp_internal.h
    REQUIRES invn-soniclib                 # chbsp_esp32_init.h pulls in <invn/soniclib/soniclib.h>
    PRIV_REQUIRES esp_driver_gpio esp_driver_spi esp_timer esp_rom log
)
