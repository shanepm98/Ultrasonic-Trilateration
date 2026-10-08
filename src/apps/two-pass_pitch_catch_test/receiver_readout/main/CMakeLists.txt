idf_component_register(SRCS "post_main.c" "readout_loop.c" "console_io.c"
                        INCLUDE_DIRS "." "../../include"
                        REQUIRES soniclib_esp32_bsp icu_post sensor_calibration invn-soniclib freertos
                                 esp_driver_gpio esp_timer log)
