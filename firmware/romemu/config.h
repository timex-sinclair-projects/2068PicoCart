// Settings that survive power-off: the current cart's name and whether the menu comes up at
// the next power-up. Without USB the cart is powered only while the 2068 is on, so anything
// meant for "next time" has to be in flash.
#pragma once
#include <stdbool.h>

#define CONFIG_NAME_LEN 64

void config_init(void);                       // read the newest valid record
const char *config_cart(void);                // "" if none
bool config_menu_next(void);
void config_set_cart(const char *name);       // each setter writes only if the value changed
void config_set_menu_next(bool on);
