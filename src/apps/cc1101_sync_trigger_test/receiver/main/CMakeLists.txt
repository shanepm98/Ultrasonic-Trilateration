idf_component_register(SRCS "main.c"
                        INCLUDE_DIRS "."
                        REQUIRES cc_link cc1101 esp_driver_gpio esp_hw_support freertos log)
