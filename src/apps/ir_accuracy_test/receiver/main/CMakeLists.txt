idf_component_register(SRCS "post_main.c" "readout_loop.c" "ir_led.c" "ir_tuner.c"
                        INCLUDE_DIRS "." "../../include"
                        REQUIRES soniclib_esp32_bsp icu_post console_io invn-soniclib freertos esp_driver_gpio esp_driver_rmt
                                 esp_timer log)
