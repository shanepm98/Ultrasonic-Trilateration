idf_component_register(SRCS "post_main.c" "readout_loop.c" "ir_led.c"
                        INCLUDE_DIRS "." "../../include"
                        REQUIRES soniclib_esp32_bsp icu_post invn-soniclib freertos esp_driver_gpio esp_driver_ledc
                                 esp_rom log)
