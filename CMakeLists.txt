cmake_minimum_required(VERSION 3.16.0)

list(APPEND EXTRA_COMPONENT_DIRS
    "${CMAKE_SOURCE_DIR}/.pio/libdeps/freenove_esp32_s3_wroom_n16r8/esp32-camera")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(AntiFrost)
