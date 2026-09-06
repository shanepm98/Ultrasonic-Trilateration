cmake_minimum_required(VERSION 3.16)

# Reuse the vendored SonicLib component that lives alongside the BSP. The BSP's chbsp_*
# sources are pulled in by the local components/soniclib_esp32_bsp wrapper (auto-discovered).
set(EXTRA_COMPONENT_DIRS
    "${CMAKE_CURRENT_LIST_DIR}/../soniclib_esp32_bsp/src/components"
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(hardware_bringup)
