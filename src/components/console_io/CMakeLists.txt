# console_io - single-keypress console input over the UART0 serial monitor (menus, number and hex
# entry). Moved here from apps/ir_accuracy_test/receiver so every app's menu shares one copy.

idf_component_register(
    SRCS "console_io.c"
    INCLUDE_DIRS "include"
    REQUIRES freertos esp_timer
)
