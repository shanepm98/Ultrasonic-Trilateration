# cc1101 - minimal SPI driver for a TI CC1101 sub-GHz transceiver (register access, command
# strobes, PATABLE, reset). Thin wrappers over esp-idf's SPI master and GPIO drivers. The SPI bus
# itself is owned by the app (it is shared with the ICU-20201); see include/cc1101.h.

idf_component_register(
    SRCS "cc1101.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_driver_spi esp_driver_gpio esp_timer freertos log
)
