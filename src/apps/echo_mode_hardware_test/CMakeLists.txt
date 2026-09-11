cmake_minimum_required(VERSION 3.16)

# Shared, reusable components (vendored SonicLib, the ESP32 BSP, the POST) live in src/components/.
set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/../../components")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(echo_mode_hardware_test)
