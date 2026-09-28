idf_component_register(SRCS "post_main.c" "sender_loop.c"
                        INCLUDE_DIRS "." "../../include"
                        REQUIRES soniclib_esp32_bsp icu_post invn-soniclib freertos esp_driver_gpio log)
