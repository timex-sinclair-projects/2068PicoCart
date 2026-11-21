// 2068 PicoCart bring-up firmware.
//
// Safe in or out of the 2068: BUS_OFF is driven high before anything else, so the
// 74LVC245 never drives the dock data bus, and every dock pin is an input.
//
// Once a second, over USB serial, it prints:
//   - VBUS and DOCK +5 V through their 10k/15k sense dividers (ADC1/ADC0)
//   - MENU, BUS_OFF and BE_GPIO levels
//   - edge counts on every dock control line and on A0-A15 over a 100 ms window
//     (all zero with the 2068 off; thousands with it running)
//   - an MD0-MD7 float test: with the buffer off, the pull-ups and pull-downs must win
// D3 (red) and D4 (white) blink alternately; holding MENU lights both.
// At start and every 10 s it scans I2C1 (GPIO34/35) for the optional OLED (0x3C/0x3D).

#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/adc.h"
#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "hardware/i2c.h"

enum {
    PIN_MD0 = 16, PIN_ROSCS = 24, PIN_MREQ = 25, PIN_RD = 26, PIN_WR = 27, PIN_IORQ = 28,
    PIN_M1 = 29, PIN_RFSH = 30, PIN_EXROM = 31, PIN_BUS_OFF = 32, PIN_BE = 33,
    PIN_SDA = 34, PIN_SCL = 35, PIN_A7R = 37, PIN_DOCK_SENSE = 40, PIN_VBUS_SENSE = 41,
    PIN_LED2 = 43, PIN_LED = 44, PIN_MENU = 45,
};

static const struct { int pin; const char *name; } CTRL[] = {
    {PIN_ROSCS, "ROSCS"}, {PIN_MREQ, "MREQ"}, {PIN_RD, "RD"}, {PIN_WR, "WR"}, {PIN_IORQ, "IORQ"},
    {PIN_M1, "M1"}, {PIN_RFSH, "RFSH"}, {PIN_EXROM, "EXROM"}, {PIN_A7R, "A7R"}, {PIN_BE, "BE"},
};
#define NCTRL (sizeof CTRL / sizeof CTRL[0])

static float sense_volts(int adc_input) {
    adc_select_input(adc_input);
    uint32_t sum = 0;
    for (int i = 0; i < 16; i++) sum += adc_read();
    float v_adc = (sum / 16.0f) * 3.3f / 4095.0f;
    return v_adc * (10.0f + 15.0f) / 15.0f;          // undo the 10k/15k divider
}

static void input_pin(int pin) {
    gpio_init(pin);
    gpio_set_dir(pin, GPIO_IN);
    gpio_disable_pulls(pin);                          // dock pins: no pulls (RP2350-E9)
}

static void i2c_scan(void) {
    printf("I2C1 scan (GPIO34 SDA / GPIO35 SCL):");
    int found = 0;
    for (int addr = 0x08; addr < 0x78; addr++) {
        uint8_t b;
        if (i2c_read_timeout_us(i2c1, addr, &b, 1, false, 2000) >= 0) {
            printf(" 0x%02x%s", addr, (addr == 0x3c || addr == 0x3d) ? " (OLED)" : "");
            found++;
        }
    }
    printf("%s\n", found ? "" : " nothing (no display fitted?)");
}

// MD0-7 float test: returns the byte read with pull-downs and with pull-ups.
static void md_float(uint8_t *lo, uint8_t *hi) {
    for (int i = 0; i < 8; i++) gpio_pull_down(PIN_MD0 + i);
    sleep_us(200);
    *lo = (gpio_get_all64() >> PIN_MD0) & 0xff;
    for (int i = 0; i < 8; i++) gpio_pull_up(PIN_MD0 + i);
    sleep_us(200);
    *hi = (gpio_get_all64() >> PIN_MD0) & 0xff;
    for (int i = 0; i < 8; i++) gpio_disable_pulls(PIN_MD0 + i);
}

int main(void) {
    // 1. keep the bus buffer off before anything else
    gpio_init(PIN_BUS_OFF);
    gpio_put(PIN_BUS_OFF, 1);
    gpio_set_dir(PIN_BUS_OFF, GPIO_OUT);

    // 2. every dock signal (A0-15, MD0-7, controls) is an input
    for (int p = 0; p <= 31; p++) input_pin(p);
    input_pin(PIN_BE);
    input_pin(PIN_A7R);

    gpio_init(PIN_MENU); gpio_set_dir(PIN_MENU, GPIO_IN); gpio_pull_up(PIN_MENU);
    gpio_init(PIN_LED); gpio_set_dir(PIN_LED, GPIO_OUT);
    gpio_init(PIN_LED2); gpio_set_dir(PIN_LED2, GPIO_OUT);

    adc_init();
    adc_gpio_init(PIN_DOCK_SENSE);                    // ADC0
    adc_gpio_init(PIN_VBUS_SENSE);                    // ADC1

    i2c_init(i2c1, 100 * 1000);
    gpio_set_function(PIN_SDA, GPIO_FUNC_I2C); gpio_pull_up(PIN_SDA);
    gpio_set_function(PIN_SCL, GPIO_FUNC_I2C); gpio_pull_up(PIN_SCL);

    stdio_init_all();
    for (int i = 0; i < 30 && !stdio_usb_connected(); i++) sleep_ms(100);
    printf("\n2068 PicoCart bring-up, %lu MHz sys clock\n", (unsigned long)(clock_get_hz(clk_sys) / 1000000));

    uint32_t tick = 0;
    for (;;) {
        // 100 ms sampling window: count edges on the controls and on the address bus
        uint32_t edges[NCTRL] = {0}, addr_changes = 0, samples = 0;
        uint64_t prev = gpio_get_all64();
        absolute_time_t end = make_timeout_time_ms(100);
        while (!time_reached(end)) {
            uint64_t now = gpio_get_all64(), diff = now ^ prev;
            if (diff) {
                for (unsigned i = 0; i < NCTRL; i++) edges[i] += (diff >> CTRL[i].pin) & 1;
                addr_changes += (diff & 0xffff) != 0;
                prev = now;
            }
            samples++;
        }

        bool menu = !gpio_get(PIN_MENU);
        gpio_put(PIN_LED, menu || (tick & 1));
        gpio_put(PIN_LED2, menu || !(tick & 1));

        float vbus = sense_volts(1), dock = sense_volts(0);
        uint8_t md_lo, md_hi;
        md_float(&md_lo, &md_hi);

        printf("[%4lu] VBUS %.2f V  DOCK %.2f V  MENU %s  BUS_OFF %d  BE %d  MD float %s (pd %02x pu %02x)\n",
               (unsigned long)tick, vbus, dock, menu ? "DOWN" : "up", gpio_get(PIN_BUS_OFF), gpio_get(PIN_BE),
               (md_lo == 0x00 && md_hi == 0xff) ? "ok" : "DRIVEN?", md_lo, md_hi);
        printf("       edges/100ms:");
        for (unsigned i = 0; i < NCTRL; i++) printf(" %s %lu", CTRL[i].name, (unsigned long)edges[i]);
        printf("  addr %lu  (levels %08lx, %lu samples)\n", (unsigned long)addr_changes,
               (unsigned long)(prev & 0xffffffff), (unsigned long)samples);

        if (tick % 10 == 0) i2c_scan();
        tick++;
        sleep_ms(900);
    }
}
