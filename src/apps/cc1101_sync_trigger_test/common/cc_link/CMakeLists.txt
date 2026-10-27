# cc_link - code shared by the cc1101_sync_trigger_test transmitter and receiver: SPI bus bring-up
# and the async-ASK CC1101 configuration (cc_ask), the ESP-NOW side channel (cc_link), and its
# message format (cc_proto). See ../../README.md.

idf_component_register(
    SRCS "cc_ask.c" "cc_cfg_ask_10k.c" "cc_cfg_fsk_50k.c" "cc_link.c" "cc_proto.c"
    INCLUDE_DIRS "include"
    REQUIRES cc1101 esp_driver_spi esp_driver_gpio esp_wifi esp_event nvs_flash freertos log
)
