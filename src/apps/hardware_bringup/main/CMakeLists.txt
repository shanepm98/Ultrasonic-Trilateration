idf_component_register(SRCS "post_main.c" "rangefinder_loop.c"
                        REQUIRES soniclib_esp32_bsp icu_post invn-soniclib freertos log)
