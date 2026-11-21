cmake_minimum_required(VERSION 3.13)

set(PICO_BOARD picocart)
set(PICO_BOARD_HEADER_DIRS ${CMAKE_CURRENT_LIST_DIR}/../boards)
set(PICO_PLATFORM rp2350-arm-s)

include(pico_sdk_import.cmake)
project(picocart_romemu C CXX ASM)
set(CMAKE_C_STANDARD 11)
pico_sdk_init()

# The dock image: ROM_IMAGE=path.dck or path.bin[@ADDR]; default the 8 KB "Hello world" AROS test cart.
set(ROM_IMAGE "${CMAKE_CURRENT_LIST_DIR}/../../../2068TapToCart/2068TapToCart/tapFiles/junkBin.bin@0x8000" CACHE STRING "dock image")
add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/rom_image.h
    COMMAND python3 ${CMAKE_CURRENT_LIST_DIR}/mkimage.py ${CMAKE_CURRENT_BINARY_DIR}/rom_image.h ${ROM_IMAGE}
    DEPENDS ${CMAKE_CURRENT_LIST_DIR}/mkimage.py VERBATIM)
add_custom_target(rom_image DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/rom_image.h)

# FatFs (read side of the cart drive) as bundled with TinyUSB
set(FATFS_DIR ${PICO_SDK_PATH}/lib/tinyusb/lib/fatfs/source)

add_executable(romemu main.c flashdisk.c loader.c usb_descriptors.c ${FATFS_DIR}/ff.c ${FATFS_DIR}/ffunicode.c)
add_dependencies(romemu rom_image)
target_include_directories(romemu PRIVATE ${CMAKE_CURRENT_LIST_DIR} ${CMAKE_CURRENT_BINARY_DIR} ${FATFS_DIR})
target_compile_definitions(romemu PRIVATE
    PICO_STDIO_USB_ENABLE_RESET_VIA_VENDOR_INTERFACE=1
    PICO_STDIO_USB_RESET_INTERFACE_SUPPORT_MS_OS_20_DESCRIPTOR=0
    PICO_STDIO_USB_ENABLE_RESET_VIA_BAUD_RATE=1)
target_link_libraries(romemu pico_stdlib pico_multicore pico_unique_id hardware_adc hardware_flash
    tinyusb_device tinyusb_board)
pico_enable_stdio_usb(romemu 1)
pico_enable_stdio_uart(romemu 0)
pico_add_extra_outputs(romemu)
