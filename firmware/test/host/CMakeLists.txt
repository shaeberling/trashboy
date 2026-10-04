# Tests of the emulator core that run on the build machine, without an ESP32:
#
#   cmake -S firmware/test/host -B firmware/build-host
#   cmake --build firmware/build-host
#   ctest --test-dir firmware/build-host --output-on-failure
#
# The sources under test are the firmware's own (components/ptrs). What they
# need from ESP-IDF and from the rest of the firmware comes from shims/ and
# host.cpp.
cmake_minimum_required(VERSION 3.16)
project(trashboy_host_tests CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(FIRMWARE ${CMAKE_CURRENT_LIST_DIR}/../..)
set(PTRS ${FIRMWARE}/components/ptrs)

add_executable(emulator_test
               emulator_test.cpp
               host.cpp
               ${PTRS}/z80.cpp
               ${PTRS}/trs.cpp
               ${PTRS}/trs_memory.cpp
               ${PTRS}/trs-keyboard.cpp)

# shims/ first: it stands in for ESP-IDF's headers
target_include_directories(emulator_test PRIVATE
                           shims
                           ${PTRS}
                           ${FIRMWARE}/managed_components/lvgl__lvgl)

# LVGL's headers, for its types, with LVGL's default configuration
target_compile_definitions(emulator_test PRIVATE LV_CONF_SKIP=1)

enable_testing()
add_test(NAME emulator COMMAND emulator_test)
