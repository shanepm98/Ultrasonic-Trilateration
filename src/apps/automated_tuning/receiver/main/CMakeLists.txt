idf_component_register(SRCS "post_main.c" "cobs.c" "serial_link.c" "at_sensor_config.c" "rpc_dispatch.c"
                             "trigger_loop.c" "espnow_relay.c"
                        INCLUDE_DIRS "." "../../include"
                        REQUIRES soniclib_esp32_bsp icu_post invn-soniclib freertos esp_driver_gpio
                                 esp_driver_uart esp_wifi esp_event esp_netif nvs_flash log)
