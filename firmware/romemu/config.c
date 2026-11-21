// Settings log in the last two 4 KB flash sectors.
//
// Each change appends a 256-byte record (one flash page) with a sequence number and a CRC;
// the newest valid record wins. A sector is erased only when the log moves into it, and the
// other sector still holds the newest record while that happens. So switching the 2068 off
// in the middle of a write loses at most that one change, never the cart that was already
// remembered. A sector holds 16 records, so each is erased once every 32 changes.
//
// Firmware before this log kept one record ("CART" + name) at the start of sector B; it is
// read as sequence 0, so an upgraded cart keeps its remembered cart.
#include <stddef.h>
#include <string.h>
#include "config.h"
#include "flashdisk.h"
#include "hardware/flash.h"
#include "hardware/sync.h"

#define SECTOR_A (CONFIG_FLASH_OFFSET - FLASH_SECTOR_SIZE)
#define SECTOR_B CONFIG_FLASH_OFFSET
#define PAGES (FLASH_SECTOR_SIZE / FLASH_PAGE_SIZE)
#define MAGIC 0x32474643u          // "CFG2"
#define LEGACY_MAGIC 0x43415254u   // "CART"
#define FLAG_MENU_NEXT 1u

typedef struct {
    uint32_t magic, seq, flags;
    char name[CONFIG_NAME_LEN];
    uint32_t crc;
} record_t;

static record_t now;                  // the newest record (magic 0 if none)
static uint32_t now_off;              // its flash offset, or 0 if none / legacy-only

static const void *flash_at(uint32_t off) { return (const void *)(XIP_BASE + off); }

static uint32_t crc32(const void *data, size_t n) {
    const uint8_t *p = data;
    uint32_t c = 0xffffffffu;
    while (n--) {
        c ^= *p++;
        for (int k = 0; k < 8; k++) c = c >> 1 ^ (0xedb88320u & -(c & 1));
    }
    return ~c;
}

static bool valid(const record_t *r) {
    return r->magic == MAGIC && r->crc == crc32(r, offsetof(record_t, crc)) &&
           memchr(r->name, 0, sizeof r->name);
}

static bool erased(uint32_t off) {
    const uint32_t *w = flash_at(off);
    for (unsigned i = 0; i < FLASH_PAGE_SIZE / 4; i++) if (w[i] != 0xffffffffu) return false;
    return true;
}

void config_init(void) {
    memset(&now, 0, sizeof now);
    now_off = 0;
    const uint32_t sectors[2] = {SECTOR_A, SECTOR_B};
    for (int s = 0; s < 2; s++)
        for (unsigned p = 0; p < PAGES; p++) {
            uint32_t off = sectors[s] + p * FLASH_PAGE_SIZE;
            const record_t *r = flash_at(off);
            if (valid(r) && (now.magic != MAGIC || r->seq > now.seq)) {
                now = *r;
                now_off = off;
            }
        }
    if (now.magic != MAGIC) {                                     // the old single record
        const struct { uint32_t magic; char name[CONFIG_NAME_LEN]; } *old = flash_at(SECTOR_B);
        if (old->magic == LEGACY_MAGIC && memchr(old->name, 0, sizeof old->name)) {
            now.magic = MAGIC;
            strcpy(now.name, old->name);
            now_off = SECTOR_B;                                   // append after it
        }
    }
}

static void write_record(void) {
    record_t r = now;
    r.magic = MAGIC;
    r.seq = now.seq + 1;
    r.crc = crc32(&r, offsetof(record_t, crc));

    // the next page after the newest record, if it is still erased; else start the other sector
    uint32_t off = 0;
    bool erase = false;
    if (now_off) {
        uint32_t sector = now_off - now_off % FLASH_SECTOR_SIZE;
        uint32_t next = now_off + FLASH_PAGE_SIZE;
        if (next < sector + FLASH_SECTOR_SIZE && erased(next)) off = next;
        else { off = sector == SECTOR_A ? SECTOR_B : SECTOR_A; erase = true; }
    } else {
        off = SECTOR_A;
        erase = true;
    }

    static uint8_t page[FLASH_PAGE_SIZE];
    memset(page, 0xff, sizeof page);
    memcpy(page, &r, sizeof r);
    uint32_t ints = save_and_disable_interrupts();               // core 1 runs from SRAM: safe
    if (erase) flash_range_erase(off, FLASH_SECTOR_SIZE);
    flash_range_program(off, page, sizeof page);
    restore_interrupts(ints);
    now = r;
    now_off = off;
}

const char *config_cart(void) { return now.name; }
bool config_menu_next(void) { return now.flags & FLAG_MENU_NEXT; }

void config_set_cart(const char *name) {
    if (strncmp(now.name, name, sizeof now.name - 1) == 0) return;
    memset(now.name, 0, sizeof now.name);
    strncpy(now.name, name, sizeof now.name - 1);
    write_record();
}

void config_set_menu_next(bool on) {
    if (config_menu_next() == on) return;
    now.flags = on ? now.flags | FLAG_MENU_NEXT : now.flags & ~FLAG_MENU_NEXT;
    write_record();
}
