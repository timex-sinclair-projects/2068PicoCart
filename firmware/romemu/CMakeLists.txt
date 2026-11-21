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

# The cart menu (Z80). Reassembled when sjasmplus is installed, else menu/menu.bin is used.
find_program(SJASMPLUS sjasmplus)
if (SJASMPLUS)
    add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/menu.bin
        COMMAND ${SJASMPLUS} --nologo --raw=${CMAKE_CURRENT_BINARY_DIR}/menu.bin ${CMAKE_CURRENT_LIST_DIR}/menu/menu.asm
        DEPENDS ${CMAKE_CURRENT_LIST_DIR}/menu/menu.asm VERBATIM)
    set(MENU_BIN ${CMAKE_CURRENT_BINARY_DIR}/menu.bin)
else()
    set(MENU_BIN ${CMAKE_CURRENT_LIST_DIR}/menu/menu.bin)
endif()
add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/menu_image.h
    COMMAND python3 ${CMAKE_CURRENT_LIST_DIR}/bin2h.py ${MENU_BIN} menu_bin ${CMAKE_CURRENT_BINARY_DIR}/menu_image.h
    DEPENDS ${MENU_BIN} ${CMAKE_CURRENT_LIST_DIR}/bin2h.py VERBATIM)
add_custom_target(menu_image DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/menu_image.h)

# The drive's Mac Finder icon (icon.c): icon/VolumeIcon.icns and the custom-icon flag file.
foreach(part icns:volume_icns:VolumeIcon.icns appledouble:appledouble_root:appledouble_root.bin)
    string(REPLACE ":" ";" part ${part})
    list(GET part 0 hname)
    list(GET part 1 cname)
    list(GET part 2 src)
    add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/icon_${hname}.h
        COMMAND python3 ${CMAKE_CURRENT_LIST_DIR}/bin2h.py ${CMAKE_CURRENT_LIST_DIR}/icon/${src} ${cname} ${CMAKE_CURRENT_BINARY_DIR}/icon_${hname}.h
        DEPENDS ${CMAKE_CURRENT_LIST_DIR}/icon/${src} ${CMAKE_CURRENT_LIST_DIR}/bin2h.py VERBATIM)
    list(APPEND ICON_HEADERS ${CMAKE_CURRENT_BINARY_DIR}/icon_${hname}.h)
endforeach()
add_custom_target(icon_files DEPENDS ${ICON_HEADERS})

# FatFs (read side of the cart drive) as bundled with TinyUSB
set(FATFS_DIR ${PICO_SDK_PATH}/lib/tinyusb/lib/fatfs/source)

add_executable(romemu main.c config.c flashdisk.c icon.c loader.c usb_descriptors.c ${FATFS_DIR}/ff.c ${FATFS_DIR}/ffunicode.c)
add_dependencies(romemu rom_image menu_image icon_files)
target_include_directories(romemu PRIVATE ${CMAKE_CURRENT_LIST_DIR} ${CMAKE_CURRENT_BINARY_DIR} ${FATFS_DIR})
# White LED (D4) brightness. Rev 0.1 boards have R15 = 100 R, far too bright: dim to 3%.
# Boards with R15 = 3k3 (the current design) want 100: -DLED2_PERCENT=100
set(LED2_PERCENT 3 CACHE STRING "white LED PWM duty, percent")
target_compile_definitions(romemu PRIVATE
    PICO_STDIO_USB_ENABLE_RESET_VIA_VENDOR_INTERFACE=1
    PICO_STDIO_USB_RESET_INTERFACE_SUPPORT_MS_OS_20_DESCRIPTOR=0
    PICO_STDIO_USB_ENABLE_RESET_VIA_BAUD_RATE=1
    LED2_PERCENT=${LED2_PERCENT})
target_link_libraries(romemu pico_stdlib pico_multicore pico_unique_id hardware_adc hardware_flash hardware_pwm
    tinyusb_device tinyusb_board)
pico_enable_stdio_usb(romemu 1)
pico_enable_stdio_uart(romemu 0)
pico_add_extra_outputs(romemu)
