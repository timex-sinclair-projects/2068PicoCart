// TinyUSB configuration: CDC (stdio) + MSC (the cart drive) + the SDK's picotool reset interface.
#ifndef TUSB_CONFIG_H
#define TUSB_CONFIG_H

#include "pico/stdio_usb.h"          // the SDK reset interface takes its defaults from here

#define CFG_TUSB_RHPORT0_MODE    OPT_MODE_DEVICE
#define CFG_TUSB_OS              OPT_OS_PICO
#define CFG_TUD_ENDPOINT0_SIZE   64

#define CFG_TUD_CDC              1
#define CFG_TUD_MSC              1
#define CFG_TUD_HID              0
#define CFG_TUD_MIDI             0
#define CFG_TUD_VENDOR           0

#define CFG_TUD_CDC_RX_BUFSIZE   256
#define CFG_TUD_CDC_TX_BUFSIZE   1024
#define CFG_TUD_CDC_EP_BUFSIZE   64

#define CFG_TUD_MSC_EP_BUFSIZE   4096

#endif
