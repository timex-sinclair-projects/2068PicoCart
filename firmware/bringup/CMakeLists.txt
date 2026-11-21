cmake_minimum_required(VERSION 3.13)

set(PICO_BOARD picocart)
set(PICO_BOARD_HEADER_DIRS ${CMAKE_CURRENT_LIST_DIR}/../boards)
set(PICO_PLATFORM rp2350-arm-s)

include(pico_sdk_import.cmake)
project(picocart_bringup C CXX ASM)
set(CMAKE_C_STANDARD 11)
pico_sdk_init()

add_executable(bringup main.c)
target_link_libraries(bringup pico_stdlib hardware_adc hardware_i2c)
pico_enable_stdio_usb(bringup 1)
pico_enable_stdio_uart(bringup 0)
pico_add_extra_outputs(bringup)
