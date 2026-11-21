// 2068 PicoCart ROM emulator with USB thumb-drive loading.
//
// Serves a 64 KB DOCK-bank image to the TS2068.
// Core 1 owns the bus: interrupts off, code and image in SRAM, polling SIO.
//   ROSCS, MREQ and RD low -> drive image[A0-A15] on MD0-MD7 until RD or ROSCS rises.
//   ROSCS, MREQ and WR low -> store MD0-MD7 if that chunk is RAM in the image.
// The 74LVC245 does the rest in hardware: enabled by ROSCS | MREQ | BUS_OFF, direction RD.
//
// Core 0 keeps BUS_OFF high until DOCK_SENSE shows a steady 2068 +5 V (and drops it at once
// when it goes), runs USB (serial + the cart drive), blinks the LEDs and prints status.
// The cart on the drive is loaded at boot; a new one copied over USB is swapped in while
// the 2068 is off (straight away, or at its next power-down). The 2068 reads the dock only
// at power-up, so switch it off and on to start the new cart. With no cart on the drive the
// image built in at compile time (rom_image.h) is served. Hold MENU at power-up to keep the
// cart off the bus.
//
// LEDs: red = cart on the bus; white = flickers with dock reads. Both blink together while a
// new cart waits for the 2068 to be power-cycled.

#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/adc.h"
#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "hardware/structs/sio.h"
#include "hardware/sync.h"
#include "tusb.h"
#include "flashdisk.h"
#include "loader.h"
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
#define RESCAN_IDLE_MS 1500                           // quiet time after host writes before a rescan

static uint8_t image[65536];                          // SRAM copy core 1 serves
static volatile uint8_t ram_chunks;
static char image_name[64];
static uint8_t image_chunks;
static cart_image_t staged;                           // a new cart, waiting for the 2068 to be off
static bool staged_pending;
static volatile uint32_t n_reads, n_writes, last_addr;

static void __not_in_flash_func(bus_core)(void) {
    save_and_disable_interrupts();
    const uint8_t ram = ram_chunks;
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
            if (ram & (1u << (a >> 13))) image[a] = d;
            while (!(sio_hw->gpio_in & (M_WR | M_ROSCS))) { }
            n_writes = ++writes; last_addr = a;
        }
    }
}

// Only while BUS_OFF is high: stop core 1, swap the image, start it again.
static void install(const uint8_t *data, uint8_t chunks, uint8_t ram, const char *name, bool running) {
    if (running) {
        multicore_reset_core1();
        sio_hw->gpio_oe_clr = M_MD;
    }
    memcpy(image, data, sizeof image);
    image_chunks = chunks;
    ram_chunks = ram;
    strncpy(image_name, name, sizeof image_name - 1);
    multicore_launch_core1(bus_core);
}

static float sense_volts(int adc_input) {
    adc_select_input(adc_input);
    uint32_t sum = 0;
    for (int i = 0; i < 8; i++) sum += adc_read();
    return (sum / 8.0f) * 3.3f / 4095.0f * 25.0f / 15.0f;
}

static uint32_t now_ms(void) { return to_ms_since_boot(get_absolute_time()); }

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

    // the cart on the drive, else the built-in image
    uint32_t t0 = time_us_32();
    flashdisk_init();
    if (loader_scan(&staged, true))
        install(staged.data, staged.chunks, staged.ram, staged.name, false);
    else
        install(rom_image_flash, ROM_IMAGE_CHUNKS, ROM_IMAGE_RAM, ROM_IMAGE_NAME " (built in)", false);
    uint32_t boot_us = time_us_32() - t0;
    bool hold_off = !gpio_get(PIN_MENU);              // MENU held at power-up: stay off the bus

    tusb_init();                                      // USB: serial (stdio) + the cart drive
    stdio_init_all();

    bool bus_on = false;
    int good = 0;
    uint32_t last_tick = now_ms(), next_print = last_tick + 1000, last_reads = 0, tick = 0;
    float dock = 0;
    for (;;) {
        tud_task();
        uint32_t t = now_ms();
        if (t == last_tick) continue;
        last_tick = t;

        // dock power: the bus goes on only after ~10 ms of steady +5 V
        dock = sense_volts(0);
        if (dock < DOCK_OFF_V || hold_off) {
            gpio_put(PIN_BUS_OFF, 1); bus_on = false; good = 0;
        } else if (!bus_on && !staged_pending && dock > DOCK_ON_V && ++good >= 10) {
            gpio_put(PIN_BUS_OFF, 0); bus_on = true;
        }

        // the drive: write back the cache, then rescan once the host has gone quiet
        flashdisk_poll(t);
        if (disk_changed && (disk_ejected || t - disk_last_write_ms > RESCAN_IDLE_MS)) {
            disk_changed = false;
            flashdisk_flush();
            if (loader_scan(&staged, false)) {
                staged_pending = true;
                if (bus_on) printf("new cart waiting: switch the 2068 off and on\n");
            }
        }
        if (staged_pending && !bus_on) {
            install(staged.data, staged.chunks, staged.ram, staged.name, true);
            staged_pending = false;
            printf("installed %s\n", image_name);
        }

        uint32_t r = n_reads;
        bool flash = staged_pending && (tick & 128);
        gpio_put(PIN_LED, staged_pending ? flash : bus_on);
        gpio_put(PIN_LED2, staged_pending ? flash : (r != last_reads && (tick & 64)));

        if ((int32_t)(t - next_print) >= 0) {
            next_print = t + 1000;
            printf("romemu [%s] chunks %02x RAM %02x  DOCK %.2f V VBUS %.2f V  bus %s%s  reads %lu (+%lu) "
                   "writes %lu  last %04lx  boot %lu us\n",
                   image_name, image_chunks, ram_chunks, dock, sense_volts(1), bus_on ? "ON" : "off",
                   hold_off ? " (MENU hold)" : "", (unsigned long)r, (unsigned long)(r - last_reads),
                   (unsigned long)n_writes, (unsigned long)last_addr, (unsigned long)boot_us);
            last_reads = r;
        }
        tick++;
    }
}
