cmake_minimum_required(VERSION 3.16)

# Shared, reusable components live in src/components/ (unused by this app - no ultrasonic
# hardware involved - but kept for template consistency with every other app).
set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/../../components")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(espnow_ftsp_test)
