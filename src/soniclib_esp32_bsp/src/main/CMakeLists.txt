idf_component_register(SRCS "esp32_bsp.c" "chbsp_esp32_init.c"
                        INCLUDE_DIRS "."
                        REQUIRES invn-soniclib esp_driver_gpio esp_driver_spi esp_timer esp_rom log)
