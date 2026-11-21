// Finds the cart on the drive and builds its 64 KB dock image.
#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint8_t data[65536];
    uint8_t chunks;        // bit n: chunk n holds cartridge data
    uint8_t ram;           // bit n: chunk n is RAM (the 2068 may write it)
    char name[64];
} cart_image_t;

// Rescans the drive. Returns true when the chosen cart differs from the one in `img`
// (by name, size and date) and has been loaded into it. `boot` picks the remembered cart.
bool loader_scan(cart_image_t *img, bool boot);
