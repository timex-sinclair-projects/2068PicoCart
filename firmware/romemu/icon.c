// The drive's icon in the Mac Finder: the cartridge in icon/picocart.svg.
//
// macOS shows a custom volume icon when the root holds .VolumeIcon.icns and the root folder's
// Finder info has the custom-icon flag set. FAT can't store Finder info, so macOS keeps it in
// an AppleDouble file called "._." at the root. Both files are added at boot when they're
// missing: on a new drive, and on drives formatted by older firmware. Both start with a dot, so
// the loader ignores them; they're also marked hidden, for Windows. They take about 31 KB.
//
// FAT doesn't allow a name to end in a dot, so macOS stores the last "." of "._." as U+F029, a
// private-use character, in the long name. FatFs can't do that (it strips trailing dots, and this
// build has no f_chmod), so it writes "._X", and a pass over the root directory then changes the
// X to U+F029 and sets the hidden bits, as macOS does. The short name and its checksum don't
// change, so the directory stays valid.
#include <string.h>
#include "ff.h"
#include "flashdisk.h"
#include "icon.h"
#include "icon_icns.h"         // volume_icns[]: icon/VolumeIcon.icns (make_icns.py)
#include "icon_appledouble.h"  // appledouble_root[]: icon/appledouble_root.bin, the flag

#define ICNS  ".VolumeIcon.icns"
#define FLAG  "._."
#define TEMP  "._X"            // FLAG, as FatFs writes it

static FATFS fs;
static FIL f;
static uint8_t dir[16384];     // the root directory (16 sectors on our drives)
static uint32_t root, secs;

static uint16_t get16(const uint8_t *p) { return p[0] | p[1] << 8; }

static void root_load(void) {
    uint8_t s[DISK_SECTOR];
    flashdisk_read(0, s, 1);
    root = get16(s + 14) + s[16] * get16(s + 22);
    secs = get16(s + 17) * 32u / DISK_SECTOR;
    if (secs > sizeof dir / DISK_SECTOR) secs = sizeof dir / DISK_SECTOR;
    flashdisk_read(root, dir, secs);
}

// The long name of the short entry `e`, from the long-name entries just before it.
static void long_name(const uint8_t *e, char *out, int max) {
    static const uint8_t pos[13] = {1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30};
    int n = 0;
    for (const uint8_t *l = e - 32; l >= dir && l[11] == 0x0f && l[0] != 0xe5; l -= 32) {
        for (int i = 0; i < 13; i++) {
            uint16_t c = get16(l + pos[i]);
            if (c == 0 || n == max - 1) goto done;
            out[n++] = c == 0xf029 ? '.' : c < 0x80 ? (char)c : '?';   // U+F029: macOS's trailing dot
        }
        if (l[0] & 0x40) break;                                   // the last part
    }
done:
    out[n] = 0;
}

// Find a root entry by long name; returns its short entry.
static uint8_t *find(const char *name) {
    char ln[32];
    for (uint8_t *e = dir; e < dir + secs * DISK_SECTOR && e[0]; e += 32) {
        if (e[0] == 0xe5 || e[11] == 0x0f || e[11] & 0x08) continue;   // deleted, long name, label
        long_name(e, ln, sizeof ln);
        if (strcmp(ln, name) == 0) return e;
    }
    return NULL;
}

static bool write_file(const char *path, const uint8_t *data, UINT len) {
    UINT done = 0;
    if (f_open(&f, path, FA_CREATE_ALWAYS | FA_WRITE) != FR_OK) return false;
    bool ok = f_write(&f, data, len, &done) == FR_OK && done == len;
    ok = f_close(&f) == FR_OK && ok;
    if (!ok) f_unlink(path);                                      // no half file, e.g. on a full drive
    return ok;
}

void icon_install(void) {
    if (f_mount(&fs, "", 1) != FR_OK) return;
    root_load();
    if (!find(ICNS)) write_file(ICNS, volume_icns, sizeof volume_icns);
    if (!find(FLAG)) write_file(TEMP, appledouble_root, sizeof appledouble_root);
    f_mount(NULL, "", 0);

    root_load();
    uint8_t *e;
    if ((e = find(TEMP)) && e[-32] == 0x41) {                     // "._X" -> "._.": third character
        e[-32 + 5] = 0x29;
        e[-32 + 6] = 0xf0;
    }
    if ((e = find(ICNS))) e[11] |= 0x02;                          // hidden
    if ((e = find(FLAG))) e[11] |= 0x02;
    flashdisk_write(root, dir, secs);                             // unchanged blocks aren't rewritten
    flashdisk_flush();
}
