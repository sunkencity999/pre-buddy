cmake_minimum_required(VERSION 3.15)
project(pre_buddy_firmware_core CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  add_compile_options(-Wall -Wextra -Wpedantic -Wshadow -Werror)
endif()

# Header-only host-testable core.
add_library(pre_buddy_core INTERFACE)
target_include_directories(pre_buddy_core
  INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/core/include)

enable_testing()

add_executable(pre_buddy_host_tests
  test/test_main.cpp
  test/test_character.cpp
  test/test_motion.cpp
  test/test_protocol.cpp
  test/test_expression.cpp
  test/test_character_picker.cpp
  test/test_character_store.cpp
  test/test_sprite_atlas.cpp
  test/test_driver_math.cpp
  test/test_hal_mock.cpp
  test/test_robot_loop.cpp
)
target_link_libraries(pre_buddy_host_tests PRIVATE pre_buddy_core)
target_include_directories(pre_buddy_host_tests
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/test)

add_test(NAME pre_buddy_host_tests COMMAND pre_buddy_host_tests)
