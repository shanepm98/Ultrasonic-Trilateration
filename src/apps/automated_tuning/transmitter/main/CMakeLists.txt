idf_component_register(SRCS "post_main.c" "espnow_link.c" "at_transmitter.c"
                        INCLUDE_DIRS "." "../../include"
                        # esp_driver_gpio is unused directly here - it's required because
                        # at_common.h (included via espnow_link.c for AT_ESPNOW_CHANNEL) also
                        # declares AT_SENDER_READY_GPIO via driver/gpio.h, same as
                        # pitch_catch_mode_hardware_test's sender requiring it for the same reason.
                        REQUIRES soniclib_esp32_bsp icu_post invn-soniclib freertos esp_driver_gpio
                                 esp_wifi esp_event esp_netif nvs_flash log)
