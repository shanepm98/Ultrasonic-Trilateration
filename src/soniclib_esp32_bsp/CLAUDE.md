# System Architecture
- The target processor is the ESP32 DevkitV1
- Application will run on the espressif's variant of FreeRTOS (https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/system/freertos_idf.html)

# Code style
- write BSP functions as wrappers around esp-idf and idf FreeRTOS functions ("https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/index.html" and "https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/system/freertos_idf.html")
- If a BSP function implementation depends on some prior initialization, indicate this with a comment and write the init function in separate source file

# Testing
- Test your code by running `./build.sh` and verifying that the code built successfully
- Once the bsp code is complete and builds successfully, remove the `app_main()` entrypoint, as it is not the main file
