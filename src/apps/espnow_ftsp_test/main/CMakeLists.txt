idf_component_register(SRCS "ftsp_main.c" "espnow_link.c" "ftsp_sync.c" "gpio_strobe.c"
                        INCLUDE_DIRS "."
                        REQUIRES freertos esp_wifi esp_event esp_netif nvs_flash esp_timer esp_driver_gpio log)

# See Kconfig.projbuild: CONFIG_FTSP_MUTE_LOGS compiles this app's own ESP_LOGx calls out
# entirely (LOG_LOCAL_LEVEL = ESP_LOG_NONE in each .c file), to rule blocking UART writes in or
# out as a source of the timestamp jitter this app's sync accuracy depends on.
if(CONFIG_FTSP_MUTE_LOGS)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE FTSP_MUTE_LOGS)
endif()
