idf_component_register(SRCS "post_main.c" "power_on_self_test.c"
                        INCLUDE_DIRS "."
                        REQUIRES soniclib_esp32_bsp invn-soniclib freertos log)
