// Board header for the 2068 PicoCart (RP2354B: RP2350B die + 2 MB flash in the package).
// Pin map: hardware/scripts/gen_mcu.py and SCHEMATIC.md sheet 3.

#ifndef _BOARDS_PICOCART_H
#define _BOARDS_PICOCART_H

pico_board_cmake_set(PICO_PLATFORM, rp2350)

#define PICOCART_2068

// --- RP2350 VARIANT: QFN-80, 48 GPIO ---
#define PICO_RP2350A 0

// --- UART: test pads TP6/TP7, GPIO46/47 = UART0 TX/RX (the bring-up firmware uses USB stdio) ---
#ifndef PICO_DEFAULT_UART
#define PICO_DEFAULT_UART 0
#endif
#ifndef PICO_DEFAULT_UART_TX_PIN
#define PICO_DEFAULT_UART_TX_PIN 46
#endif
#ifndef PICO_DEFAULT_UART_RX_PIN
#define PICO_DEFAULT_UART_RX_PIN 47
#endif

// --- LEDs: D3 red on GPIO44 (default), D4 white on GPIO43 ---
#ifndef PICO_DEFAULT_LED_PIN
#define PICO_DEFAULT_LED_PIN 44
#endif

// --- I2C: optional OLED on J3, I2C1 SDA GPIO34 / SCL GPIO35 ---
#ifndef PICO_DEFAULT_I2C
#define PICO_DEFAULT_I2C 1
#endif
#ifndef PICO_DEFAULT_I2C_SDA_PIN
#define PICO_DEFAULT_I2C_SDA_PIN 34
#endif
#ifndef PICO_DEFAULT_I2C_SCL_PIN
#define PICO_DEFAULT_I2C_SCL_PIN 35
#endif

// --- FLASH: 2 MB in package ---
#define PICO_BOOT_STAGE2_CHOOSE_W25Q080 1
#ifndef PICO_FLASH_SPI_CLKDIV
#define PICO_FLASH_SPI_CLKDIV 2
#endif
pico_board_cmake_set_default(PICO_FLASH_SIZE_BYTES, (2 * 1024 * 1024))
#ifndef PICO_FLASH_SIZE_BYTES
#define PICO_FLASH_SIZE_BYTES (2 * 1024 * 1024)
#endif

// --- CRYSTAL: 12 MHz YXC X322512MOB4SI; allow a slow start ---
#ifndef PICO_XOSC_STARTUP_DELAY_MULTIPLIER
#define PICO_XOSC_STARTUP_DELAY_MULTIPLIER 64
#endif

// No SMPS mode pin, VBUS pin or VSYS pin: GPIO23-29 are dock signals on this board.

pico_board_cmake_set_default(PICO_RP2350_A2_SUPPORTED, 1)
#ifndef PICO_RP2350_A2_SUPPORTED
#define PICO_RP2350_A2_SUPPORTED 1
#endif

#endif
