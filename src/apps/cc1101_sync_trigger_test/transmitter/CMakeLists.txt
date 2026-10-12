cmake_minimum_required(VERSION 3.16)

# Shared, reusable components (cc1101 driver, console_io) live in src/components/; the code shared by
# this test's transmitter and receiver (cc_link) lives in ../common/.
set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/../../../components" "${CMAKE_CURRENT_LIST_DIR}/../common")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(cc1101_sync_trigger_transmitter)
