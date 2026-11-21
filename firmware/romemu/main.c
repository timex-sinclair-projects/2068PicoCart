// 2068 PicoCart ROM emulator with USB thumb-drive loading and an on-screen cart menu.
//
// Serves a 64 KB DOCK-bank image to the TS2068.
// Core 1 owns the bus: interrupts off, code and image in SRAM, polling SIO.
//   ROSCS, MREQ and RD low -> drive image[A0-A15] on MD0-MD7 until RD or ROSCS rises.
//   ROSCS, MREQ and WR low -> store MD0-MD7 if that chunk is RAM in the image; a write to
//                             the mailbox ($BFFF) is also passed to core 0.
// The 74LVC245 does the rest in hardware: enabled by ROSCS | MREQ | BUS_OFF, direction RD.
//
// Core 0 keeps BUS_OFF high until DOCK_SENSE shows a steady 2068 +5 V (and drops it at once
// when it goes), runs USB (serial + the cart drive), blinks the LEDs and prints status.
// Each time the 2068 powers up, core 0 picks what to serve:
//   - the cart menu (menu/menu.asm) if MENU is held, or was pressed while the 2068 was on;
//   - otherwise the current cart: the one chosen in the menu or copied over USB last, or
//     the image built in at compile time (rom_image.h) if the drive has none.
// The menu lists the carts on the drive. Choosing one writes its number to the mailbox; the
// cart swaps the image and the menu restarts the 2068, which then reads the new cart.
// Entry 0 starts the 2068 without the cart, until its next power-up.
//
// LEDs: red = cart on the bus; white = flickers with dock reads. Both blink together while
// something waits for the next power-up (a cart copied over USB, or the menu).

#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/adc.h"
#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "hardware/pwm.h"
#include "hardware/structs/sio.h"
#include "hardware/sync.h"
#include "tusb.h"
#include "config.h"
#include "flashdisk.h"
#include "icon.h"
#include "loader.h"
#include "menu_image.h"
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
#define LED2_WRAP 999                                 // white LED PWM: 150 kHz at 150 MHz
#define LED2_ON (LED2_PERCENT * (LED2_WRAP + 1) / 100)  // LED2_PERCENT from CMake: 3 for R15 = 100 R, 100 for 3k3
#define RESCAN_IDLE_MS 1500                           // quiet time after host writes before a rescan

// the menu's directory table (see menu/menu.asm)
#define MENU_ORG 0x8000
#define MENU_DIR 0x9000
#define MENU_NAMES 0x9010
#define MENU_NAME_LEN 32
#define MENU_MAX 100
#define MAILBOX 0xbfff

static uint8_t image[65536];                          // SRAM copy core 1 serves
static volatile uint8_t ram_chunks;
static volatile uint32_t n_reads, n_writes, last_addr;
static volatile uint32_t mailbox;                     // 0x100 | byte when the 2068 writes $BFFF

static cart_image_t cart;                             // the current cart, as loaded
static cart_image_t staged;                           // a cart copied over USB, for the next power-up
static cart_image_t menu;                             // the menu, with the directory filled in
static char menu_names[MENU_MAX][64];
static int menu_count;
static bool staged_pending;

enum serving { SERVE_NONE, SERVE_CART, SERVE_MENU };
static enum serving serving = SERVE_NONE;
static const char *const SERVING_NAME[] = {"none", "cart", "menu"};
static char image_name[64];
static uint8_t image_chunks;

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
            if (a == MAILBOX) mailbox = 0x100u | d;
            while (!(sio_hw->gpio_in & (M_WR | M_ROSCS))) { }
            n_writes = ++writes; last_addr = a;
        }
    }
}

// Only while BUS_OFF is high: stop core 1, swap the image, start it again.
static void install(const cart_image_t *img, enum serving what) {
    static bool running;
    if (running) {
        multicore_reset_core1();
        sio_hw->gpio_oe_clr = M_MD;
    }
    memcpy(image, img->data, sizeof image);
    image_chunks = img->chunks;
    ram_chunks = img->ram;
    strncpy(image_name, img->name, sizeof image_name - 1);
    mailbox = 0;
    multicore_launch_core1(bus_core);
    running = true;
    serving = what;
}

// The menu image: the menu program at $8000 and the drive's carts in its directory table.
// Built only when the menu is about to be served, so menu_names always matches what the
// 2068 shows, even if the drive changes while the menu is up.
static void build_menu(void) {
    memset(menu.data, 0xff, sizeof menu.data);
    memcpy(menu.data + MENU_ORG, menu_bin, sizeof menu_bin);
    menu_count = loader_list(menu_names, MENU_MAX);
    const char *current = loader_current();
    uint8_t cur = 0;
    memset(menu.data + MENU_DIR, 0, MENU_NAMES - MENU_DIR + MENU_MAX * MENU_NAME_LEN);
    for (int i = 0; i < menu_count; i++) {
        strncpy((char *)menu.data + MENU_NAMES + i * MENU_NAME_LEN, menu_names[i], MENU_NAME_LEN - 1);
        if (strcmp(menu_names[i], current) == 0) cur = (uint8_t)(i + 1);
    }
    menu.data[MENU_DIR] = (uint8_t)menu_count;
    menu.data[MENU_DIR + 1] = cur;
    menu.chunks = 0x30;                               // chunks 4 and 5 (the mailbox)
    menu.ram = 0;
    strcpy(menu.name, "cart menu");
}

static float sense_volts(int adc_input) {
    adc_select_input(adc_input);
    uint32_t sum = 0;
    for (int i = 0; i < 8; i++) sum += adc_read();
    return (sum / 8.0f) * 3.3f / 4095.0f * 25.0f / 15.0f;
}

static uint32_t now_ms(void) { return to_ms_since_boot(get_absolute_time()); }

// The 2068 has just powered up (steady +5 V, bus still off): choose what to serve.
static void power_up(void) {
    if (staged_pending) {
        memcpy(&cart, &staged, sizeof cart);
        staged_pending = false;
    }
    bool want_menu = !gpio_get(PIN_MENU) || config_menu_next();
    config_set_menu_next(false);
    if (want_menu) {
        build_menu();
        install(&menu, SERVE_MENU);
    }
    else install(&cart, SERVE_CART);
    printf("2068 on: serving %s\n", image_name);
}

// The menu wrote its choice to the mailbox. The 2068 is waiting in HOME RAM and restarts
// in about a second, so swap the image now.
static void menu_choice(uint8_t entry) {
    if (entry == 0) {
        gpio_put(PIN_BUS_OFF, 1);
        serving = SERVE_NONE;                        // off the bus until the next power-up
        printf("menu: no cartridge\n");
        return;
    }
    if (entry > menu_count || !loader_load_name(menu_names[entry - 1], &staged)) {
        printf("menu: entry %u failed, back to the menu\n", entry);
        staged_pending = false;                       // `staged` may hold part of the failed file
        return;                                       // the 2068 restarts into the menu again
    }
    memcpy(&cart, &staged, sizeof cart);
    staged_pending = false;                           // the menu choice wins over a USB copy
    gpio_put(PIN_BUS_OFF, 1);
    install(&cart, SERVE_CART);
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
    gpio_set_function(PIN_LED2, GPIO_FUNC_PWM);
    uint led2_slice = pwm_gpio_to_slice_num(PIN_LED2);
    pwm_set_wrap(led2_slice, LED2_WRAP);
    pwm_set_gpio_level(PIN_LED2, 0);
    pwm_set_enabled(led2_slice, true);
    adc_init();
    adc_gpio_init(PIN_DOCK_SENSE);
    adc_gpio_init(PIN_VBUS_SENSE);

    // the cart on the drive, else the built-in image; served once the 2068 is on
    uint32_t t0 = time_us_32();
    flashdisk_init();
    icon_install();                                   // the drive's Mac Finder icon
    config_init();
    if (!loader_scan(&cart, true)) {
        memcpy(cart.data, rom_image_flash, sizeof cart.data);
        cart.chunks = ROM_IMAGE_CHUNKS;
        cart.ram = ROM_IMAGE_RAM;
        strcpy(cart.name, ROM_IMAGE_NAME " (built in)");
    }
    uint32_t boot_us = time_us_32() - t0;

    tusb_init();                                      // USB: serial (stdio) + the cart drive
    stdio_init_all();

    bool bus_on = false, powered = false;
    int good = 0, menu_low = 0;
    bool menu_was_down = !gpio_get(PIN_MENU);
    uint32_t last_tick = now_ms(), next_print = last_tick + 1000, last_reads = 0, tick = 0;
    float dock = 0;
    for (;;) {
        tud_task();
        uint32_t t = now_ms();
        if (t == last_tick) continue;
        last_tick = t;

        // dock power: what to serve is chosen after ~10 ms of steady +5 V
        dock = sense_volts(0);
        if (dock < DOCK_OFF_V) {
            powered = false; good = 0;
        } else if (dock > DOCK_ON_V && !powered && ++good >= 10) {
            powered = true;
            power_up();
            menu_was_down = !gpio_get(PIN_MENU);      // a hold at power-up is not a press
        }
        bool want_bus = powered && serving != SERVE_NONE;
        if (want_bus != bus_on) {
            gpio_put(PIN_BUS_OFF, !want_bus);
            bus_on = want_bus;
        }

        // the menu's choice
        uint32_t mb = mailbox;
        if (mb && serving == SERVE_MENU) {
            mailbox = 0;
            menu_choice((uint8_t)mb);
            bus_on = false;                           // back on next tick if still serving
        }

        // MENU pressed (debounced): the menu at the next power-up; press again to cancel
        menu_low = gpio_get(PIN_MENU) ? 0 : menu_low + 1;
        bool menu_down = menu_low >= 20;
        if (menu_down && !menu_was_down && serving != SERVE_MENU) {
            config_set_menu_next(!config_menu_next());
            printf(config_menu_next() ? "MENU: the menu comes up at the next power-up\n" : "MENU: cancelled\n");
        }
        if (gpio_get(PIN_MENU)) menu_was_down = false;
        else if (menu_down) menu_was_down = true;

        // the drive: write back the cache, then rescan once the host has gone quiet
        flashdisk_poll(t);
        if (disk_changed && (disk_ejected || t - disk_last_write_ms > RESCAN_IDLE_MS)) {
            disk_changed = false;
            flashdisk_flush();
            if (loader_scan(&staged, false)) {
                staged_pending = true;
                printf("new cart waiting: switch the 2068 off and on\n");
            }
        }

        uint32_t r = n_reads;
        bool waiting = staged_pending || config_menu_next();
        bool flash = tick & 128;
        gpio_put(PIN_LED, waiting ? flash : bus_on);
        pwm_set_gpio_level(PIN_LED2, (waiting ? flash : (r != last_reads && (tick & 64))) ? LED2_ON : 0);

        if ((int32_t)(t - next_print) >= 0) {
            next_print = t + 1000;
            printf("romemu %s [%s] chunks %02x RAM %02x  DOCK %.2f V VBUS %.2f V  bus %s  reads %lu (+%lu) "
                   "writes %lu  last %04lx  boot %lu us\n",
                   SERVING_NAME[serving], image_name, image_chunks, ram_chunks, dock, sense_volts(1),
                   bus_on ? "ON" : "off", (unsigned long)r, (unsigned long)(r - last_reads),
                   (unsigned long)n_writes, (unsigned long)last_addr, (unsigned long)boot_us);
            last_reads = r;
        }
        tick++;
    }
}
