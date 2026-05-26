# PRE Buddy — ESP-IDF top-level project file.
#
# Not picked up by the host-test build (see ../CMakeLists.txt). This file
# is intended for use under `idf.py` only:
#
#     cd firmware/esp32
#     idf.py set-target esp32s3
#     idf.py build
#     idf.py -p /dev/cu.usbmodem* flash monitor

cmake_minimum_required(VERSION 3.16)

# IDF expects EXTRA_COMPONENT_DIRS to find sibling components.
set(EXTRA_COMPONENT_DIRS
  ${CMAKE_CURRENT_LIST_DIR}/main
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake OPTIONAL)
project(pre_buddy_device CXX C)
