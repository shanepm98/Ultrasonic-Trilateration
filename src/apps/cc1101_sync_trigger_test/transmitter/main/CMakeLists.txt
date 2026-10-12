idf_component_register(SRCS "main.c" "tx_menu.c" "ook_tx.c"
                        INCLUDE_DIRS "."
                        REQUIRES cc_link cc1101 console_io esp_driver_rmt esp_timer freertos log)
