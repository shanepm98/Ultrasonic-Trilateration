idf_component_register(SRCS "post_main.c" "receiver_loop.c"
                        INCLUDE_DIRS "." "../../include"
                        REQUIRES soniclib_esp32_bsp icu_post sensor_calibration invn-soniclib freertos esp_driver_gpio log)
