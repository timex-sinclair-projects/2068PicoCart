// USB descriptors: a composite CDC-ACM (USB serial, used by stdio) + mass storage (the cart
// drive) + the SDK's vendor reset interface, so `picotool load -f` can still reboot the cart.
#include <string.h>
#include "tusb.h"
#include "pico/unique_id.h"
#include "pico/usb_reset_interface.h"

enum { ITF_CDC = 0, ITF_CDC_DATA, ITF_MSC, ITF_RESET, ITF_COUNT };
enum { STR_LANG, STR_MANUF, STR_PRODUCT, STR_SERIAL, STR_CDC, STR_MSC, STR_RESET };

#define EP_CDC_NOTIF 0x81
#define EP_CDC_OUT   0x02
#define EP_CDC_IN    0x82
#define EP_MSC_OUT   0x03
#define EP_MSC_IN    0x83

static const tusb_desc_device_t desc_device = {
    .bLength = sizeof(tusb_desc_device_t),
    .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB = 0x0200,
    .bDeviceClass = TUSB_CLASS_MISC,
    .bDeviceSubClass = MISC_SUBCLASS_COMMON,
    .bDeviceProtocol = MISC_PROTOCOL_IAD,
    .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor = 0x2E8A,                 // Raspberry Pi
    .idProduct = 0x0009,                // Pico SDK CDC (picotool knows it)
    .bcdDevice = 0x0110,
    .iManufacturer = STR_MANUF,
    .iProduct = STR_PRODUCT,
    .iSerialNumber = STR_SERIAL,
    .bNumConfigurations = 1,
};

const uint8_t *tud_descriptor_device_cb(void) { return (const uint8_t *)&desc_device; }

#define RESET_DESC_LEN 9
#define RESET_DESCRIPTOR(itf, str) \
    9, TUSB_DESC_INTERFACE, itf, 0, 0, TUSB_CLASS_VENDOR_SPECIFIC, RESET_INTERFACE_SUBCLASS, RESET_INTERFACE_PROTOCOL, str
#define CONFIG_LEN (TUD_CONFIG_DESC_LEN + TUD_CDC_DESC_LEN + TUD_MSC_DESC_LEN + RESET_DESC_LEN)

static const uint8_t desc_config[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_COUNT, 0, CONFIG_LEN, 0, 250),
    TUD_CDC_DESCRIPTOR(ITF_CDC, STR_CDC, EP_CDC_NOTIF, 8, EP_CDC_OUT, EP_CDC_IN, 64),
    TUD_MSC_DESCRIPTOR(ITF_MSC, STR_MSC, EP_MSC_OUT, EP_MSC_IN, 64),
    RESET_DESCRIPTOR(ITF_RESET, STR_RESET),
};

const uint8_t *tud_descriptor_configuration_cb(uint8_t index) { (void)index; return desc_config; }

static const char *const strings[] = {
    [STR_MANUF] = "2068 PicoCart", [STR_PRODUCT] = "PicoCart", [STR_SERIAL] = NULL,
    [STR_CDC] = "PicoCart Serial", [STR_MSC] = "PicoCart Drive", [STR_RESET] = "Reset",
};

const uint16_t *tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
    (void)langid;
    static uint16_t desc[33];
    static char serial[2 * PICO_UNIQUE_BOARD_ID_SIZE_BYTES + 1];
    unsigned len;
    if (index == STR_LANG) {
        desc[1] = 0x0409;
        len = 1;
    } else {
        if (index >= sizeof strings / sizeof strings[0]) return NULL;
        const char *s = strings[index];
        if (index == STR_SERIAL) {
            if (!serial[0]) pico_get_unique_board_id_string(serial, sizeof serial);
            s = serial;
        }
        for (len = 0; len < 32 && s[len]; len++) desc[1 + len] = (uint8_t)s[len];
    }
    desc[0] = (uint16_t)((TUSB_DESC_STRING << 8) | (2 * len + 2));
    return desc;
}
