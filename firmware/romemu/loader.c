// Picks the cart file on the drive and parses it.
//
// Candidates: files in the root whose names end .DCK, .BIN or .ROM, skipping dot files (macOS
// "._" AppleDouble files), hidden and system files. The cart is the file that appeared or
// changed most recently. Finder keeps a copied file's original date, so dates alone cannot
// say which file is new: each scan is compared with the previous one. At boot the cart
// remembered in the settings log (config.c) wins, else the newest-dated file.
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "loader.h"
#include "config.h"
#include "flashdisk.h"
#include "ff.h"

#define MAX_FILES 48

typedef struct {
    char name[64];
    uint32_t size, stamp;          // stamp = FAT date << 16 | time
} entry_t;

static entry_t prev[MAX_FILES], cur[MAX_FILES];
static int n_prev = -1, n_cur;
static entry_t chosen;
static FATFS fs;

static bool ends_with(const char *s, const char *ext) {
    size_t n = strlen(s), m = strlen(ext);
    if (n < m) return false;
    for (size_t i = 0; i < m; i++) if (tolower((unsigned char)s[n - m + i]) != ext[i]) return false;
    return true;
}

static bool same(const entry_t *a, const entry_t *b) {
    return a->size == b->size && a->stamp == b->stamp && strcmp(a->name, b->name) == 0;
}

static const entry_t *newest(const entry_t *list, int n, bool only_new) {
    const entry_t *best = NULL;
    for (int i = 0; i < n; i++) {
        if (only_new) {
            bool seen = false;
            for (int j = 0; j < n_prev && !seen; j++) seen = same(&list[i], &prev[j]);
            if (seen) continue;
        }
        if (!best || list[i].stamp > best->stamp ||
            (list[i].stamp == best->stamp && strcmp(list[i].name, best->name) < 0))
            best = &list[i];
    }
    return best;
}

// ---- parsing ----

static bool read_exact(FIL *f, void *buf, UINT n) {
    UINT got;
    return f_read(f, buf, n, &got) == FR_OK && got == n;
}

// DCK: blocks of [bank][type x 8] followed by the 8 KB chunks the types say are in the file.
// Type bit 0 = RAM, bit 1 = contents in the file (0 absent, 1 empty RAM, 2 ROM, 3 RAM with data).
// Only bank 0 (DOCK) is used; other banks (EXROM, HOME) are skipped.
static bool parse_dck(FIL *f, cart_image_t *img) {
    uint8_t hdr[9];
    bool found = false;
    while (!found && read_exact(f, hdr, 9)) {
        for (int n = 0; n < 8; n++) {
            uint8_t t = hdr[1 + n];
            if (hdr[0] == 0) {
                if (t & 1) img->ram |= 1u << n;
                if (t & 2) {
                    if (!read_exact(f, img->data + n * 8192, 8192)) return false;
                    img->chunks |= 1u << n;
                }
            } else if (t & 2 && f_lseek(f, f_tell(f) + 8192) != FR_OK) {
                return false;
            }
        }
        found = hdr[0] == 0;
    }
    return found && img->chunks;
}

// BIN/ROM: raw bytes at the address after an '@' in the name (hex, e.g. GAME@0000.BIN),
// else at $8000 (AROS) when it fits there, else at $0000.
static bool parse_bin(FIL *f, const char *name, cart_image_t *img) {
    uint32_t size = (uint32_t)f_size(f), addr = size <= 0x8000 ? 0x8000 : 0;
    const char *at = strchr(name, '@');
    if (at) {
        char *end;
        unsigned long a = strtoul(at[1] == '$' ? at + 2 : at + 1, &end, 16);
        if (end != at + 1) addr = (uint32_t)a;
    }
    if (size == 0 || addr + size > 65536) return false;
    if (!read_exact(f, img->data + addr, size)) return false;
    for (uint32_t n = addr / 8192; n < (addr + size + 8191) / 8192; n++) img->chunks |= 1u << n;
    return true;
}

static bool load(const entry_t *e, cart_image_t *img) {
    FIL f;
    if (f_open(&f, e->name, FA_READ) != FR_OK) return false;
    memset(img->data, 0xff, sizeof img->data);                    // what an empty dock reads
    img->chunks = img->ram = 0;
    bool ok = ends_with(e->name, ".dck") ? parse_dck(&f, img) : parse_bin(&f, e->name, img);
    f_close(&f);
    if (ok) {
        strncpy(img->name, e->name, sizeof img->name - 1);
        img->name[sizeof img->name - 1] = 0;
    }
    return ok;
}

bool loader_scan(cart_image_t *img, bool boot) {
    n_cur = 0;
    f_mount(NULL, "", 0);
    if (f_mount(&fs, "", 1) != FR_OK) { printf("drive: no FAT volume\n"); return false; }
    DIR d;
    FILINFO fi;
    if (f_opendir(&d, "/") == FR_OK) {
        while (n_cur < MAX_FILES && f_readdir(&d, &fi) == FR_OK && fi.fname[0]) {
            if (fi.fattrib & (AM_DIR | AM_HID | AM_SYS) || fi.fname[0] == '.') continue;
            if (!ends_with(fi.fname, ".dck") && !ends_with(fi.fname, ".bin") && !ends_with(fi.fname, ".rom")) continue;
            if (strlen(fi.fname) >= sizeof cur[0].name) continue;
            entry_t *e = &cur[n_cur++];
            strcpy(e->name, fi.fname);
            e->size = (uint32_t)fi.fsize;
            e->stamp = (uint32_t)fi.fdate << 16 | fi.ftime;
        }
        f_closedir(&d);
    }

    const entry_t *pick = NULL;
    if (boot) {
        for (int i = 0; i < n_cur && !pick; i++)
            if (strcmp(cur[i].name, config_cart()) == 0) pick = &cur[i];
    } else {
        pick = newest(cur, n_cur, true);                          // a file that appeared or changed
        for (int i = 0; i < n_cur && !pick && chosen.name[0]; i++)
            if (strcmp(cur[i].name, chosen.name) == 0) pick = &cur[i];   // keep the current cart
    }
    if (!pick) pick = newest(cur, n_cur, false);

    bool changed = false;
    if (pick && !same(pick, &chosen)) {
        entry_t e = *pick;
        if (load(&e, img)) {
            chosen = e;
            changed = true;
            config_set_cart(e.name);
            printf("drive: cart %s (%lu bytes, chunks %02x, RAM %02x)\n", e.name, (unsigned long)e.size,
                   img->chunks, img->ram);
        } else {
            printf("drive: %s is not a cart image I can use\n", e.name);
            if (boot) chosen = e;                                 // don't retry it on every scan
        }
    }
    memcpy(prev, cur, sizeof cur[0] * n_cur);
    n_prev = n_cur;
    f_mount(NULL, "", 0);
    return changed;
}

static int by_name(const void *a, const void *b) {
    const char *x = a, *y = b;
    for (; *x && *y; x++, y++) {
        int d = tolower((unsigned char)*x) - tolower((unsigned char)*y);
        if (d) return d;
    }
    return (unsigned char)*x - (unsigned char)*y;
}

int loader_list(char (*names)[64], int max) {
    int n = n_cur < max ? n_cur : max;
    for (int i = 0; i < n; i++) strcpy(names[i], cur[i].name);
    qsort(names, (size_t)n, sizeof names[0], by_name);
    return n;
}

bool loader_load_name(const char *name, cart_image_t *img) {
    for (int i = 0; i < n_cur; i++) {
        if (strcmp(cur[i].name, name) != 0) continue;
        entry_t e = cur[i];
        if (f_mount(&fs, "", 1) != FR_OK) return false;
        bool ok = load(&e, img);
        f_mount(NULL, "", 0);
        if (ok) {
            chosen = e;
            config_set_cart(e.name);
            printf("menu: cart %s (chunks %02x, RAM %02x)\n", e.name, img->chunks, img->ram);
        }
        return ok;
    }
    return false;
}

const char *loader_current(void) { return chosen.name; }
