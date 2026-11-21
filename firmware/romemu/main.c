// 2068 PicoCart ROM emulator, first light.
//
// Serves a 64 KB DOCK-bank image (rom_image.h, built by mkimage.py) to the TS2068.
// Core 1 owns the bus: interrupts off, code and image in SRAM, polling SIO.
//   ROSCS, MREQ and RD low -> drive image[A0-A15] on MD0-MD7 until RD or ROSCS rises.
//   ROSCS, MREQ and WR low -> store MD0-MD7 if that chunk is RAM in the image.
// The 74LVC245 does the rest in hardware: enabled by ROSCS | MREQ | BUS_OFF, direction RD.
// Core 0 keeps BUS_OFF high until DOCK_SENSE shows a steady 2068 +5 V (and drops it at once
// when it goes), blinks the LEDs and prints status over USB. Hold MENU at power-up to keep the
// cart off the bus.

#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/adc.h"
#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "hardware/structs/sio.h"
#include "hardware/sync.h"
#include "rom_image.h"

enum {
    PIN_MD0 = 16, PIN_ROSCS = 24, PIN_MREQ = 25, PIN_RD = 26, PIN_WR = 27,
    PIN_BUS_OFF = 32, PIN_DOCK_SENSE = 40, PIN_VBUS_SENSE = 41, PIN_LED2 = 43, PIN_LED = 44, PIN_MENU = 45,
};
#define M_ROSCS (1u << PIN_ROSCS)
#define M_MREQ (1u << PIN_MREQ)
#define M_RD (1u << PIN_RD)
#define M_WR (1u << PIN_WR)
#define M_MD (0xffu << PIN_MD0)
#define DOCK_ON_V 4.4f
#define DOCK_OFF_V 4.0f

static uint8_t image[65536];                          // SRAM copy of the image
static volatile uint32_t n_reads, n_writes, last_addr;

static void __not_in_flash_func(bus_core)(void) {
    save_and_disable_interrupts();
    const uint8_t ram_chunks = ROM_IMAGE_RAM;
    uint32_t reads = 0, writes = 0;
    for (;;) {
        uint32_t g = sio_hw->gpio_in;
        if (!(g & (M_ROSCS | M_MREQ | M_RD))) {                       // dock read
            uint32_t a = g & 0xffff;
            sio_hw->gpio_clr = M_MD;
            sio_hw->gpio_set = (uint32_t)image[a] << PIN_MD0;
            sio_hw->gpio_oe_set = M_MD;
            while (!(sio_hw->gpio_in & (M_RD | M_ROSCS))) { }
            sio_hw->gpio_oe_clr = M_MD;
            n_reads = ++reads; last_addr = a;
        } else if (!(g & (M_ROSCS | M_MREQ | M_WR))) {                // dock write
            uint32_t a = g & 0xffff;
            for (volatile int i = 0; i < 4; i++) { }                // let the buffer settle
            uint8_t d = (sio_hw->gpio_in >> PIN_MD0) & 0xff;
            if (ram_chunks & (1u << (a >> 13))) image[a] = d;
            while (!(sio_hw->gpio_in & (M_WR | M_ROSCS))) { }
            n_writes = ++writes; last_addr = a;
        }
    }
}

static float sense_volts(int adc_input) {
    adc_select_input(adc_input);
    uint32_t sum = 0;
    for (int i = 0; i < 8; i++) sum += adc_read();
    return (sum / 8.0f) * 3.3f / 4095.0f * 25.0f / 15.0f;
}

int main(void) {
    gpio_init(PIN_BUS_OFF);
    gpio_put(PIN_BUS_OFF, 1);                         // bus buffer off until dock power is proven
    gpio_set_dir(PIN_BUS_OFF, GPIO_OUT);
    for (int p = 0; p <= 31; p++) {
        gpio_init(p);
        gpio_set_dir(p, GPIO_IN);
        gpio_disable_pulls(p);
        if (p >= PIN_MD0 && p < PIN_MD0 + 8) gpio_set_slew_rate(p, GPIO_SLEW_RATE_FAST);
    }
    gpio_init(PIN_MENU); gpio_set_dir(PIN_MENU, GPIO_IN); gpio_pull_up(PIN_MENU);
    gpio_init(PIN_LED); gpio_set_dir(PIN_LED, GPIO_OUT);
    gpio_init(PIN_LED2); gpio_set_dir(PIN_LED2, GPIO_OUT);
    adc_init();
    adc_gpio_init(PIN_DOCK_SENSE);
    adc_gpio_init(PIN_VBUS_SENSE);

    memcpy(image, rom_image_flash, sizeof image);
    sleep_us(50);
    bool hold_off = !gpio_get(PIN_MENU);              // MENU held at power-up: stay off the bus
    multicore_launch_core1(bus_core);

    // bring the bus up as early as possible: the 2068 reads the ROS header soon after reset
    bool bus_on = false;
    int good = 0;
    absolute_time_t next_print = make_timeout_time_ms(1000);
    bool stdio_up = false;
    uint32_t last_reads = 0, tick = 0;
    for (;;) {
        float dock = sense_volts(0);
        if (dock < DOCK_OFF_V || hold_off) {
            gpio_put(PIN_BUS_OFF, 1); bus_on = false; good = 0;
        } else if (!bus_on && dock > DOCK_ON_V && ++good >= 10) {   // ~10 ms steady
            gpio_put(PIN_BUS_OFF, 0); bus_on = true;
        }
        gpio_put(PIN_LED, bus_on);
        uint32_t r = n_reads;
        gpio_put(PIN_LED2, r != last_reads && (tick & 64));
        if (!stdio_up && to_ms_since_boot(get_absolute_time()) > 300) {
            stdio_init_all();                         // after the bus is up
            stdio_up = true;
        }
        if (stdio_up && time_reached(next_print)) {
            next_print = make_timeout_time_ms(1000);
            printf("romemu [%s] chunks %02x  DOCK %.2f V VBUS %.2f V  bus %s%s  reads %lu (+%lu) writes %lu  last %04lx\n",
                   ROM_IMAGE_NAME, ROM_IMAGE_CHUNKS, dock, sense_volts(1), bus_on ? "ON" : "off",
                   hold_off ? " (MENU hold)" : "", (unsigned long)r, (unsigned long)(r - last_reads),
                   (unsigned long)n_writes, (unsigned long)last_addr);
            last_reads = r;
        }
        tick++;
        sleep_ms(1);
    }
}
