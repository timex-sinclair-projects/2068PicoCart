// Flash-backed FAT12 drive.
//
// Reads come straight from XIP flash, except for the one 4 KB erase block held in the write
// cache. Writes fill that cache (read-modify-write); it goes back to flash when the host
// writes to another block, after 150 ms without writes, or on SYNCHRONIZE CACHE and eject.
// Erasing and programming run on core 0 with interrupts off. That is safe while the cart
// is serving the 2068: core 1 runs only from SRAM and never touches flash.
#include <string.h>
#include "flashdisk.h"
#include "hardware/flash.h"
#include "hardware/sync.h"
#include "pico/stdlib.h"
#include "tusb.h"
#include "ff.h"
#include "diskio.h"

#define BLOCK  FLASH_SECTOR_SIZE                   // 4096: one erase block = 8 disk sectors
#define SPB    (BLOCK / DISK_SECTOR)

static uint8_t cache[BLOCK];
static int32_t cache_blk = -1;
static bool cache_dirty;
volatile bool disk_changed, disk_ejected;
volatile uint32_t disk_last_write_ms;

static const uint8_t *flash_ptr(uint32_t off) { return (const uint8_t *)(XIP_BASE + DISK_FLASH_OFFSET + off); }

void flashdisk_flush(void) {
    if (!cache_dirty) return;
    cache_dirty = false;
    uint32_t off = (uint32_t)cache_blk * BLOCK;
    if (memcmp(cache, flash_ptr(off), BLOCK) == 0) return;       // the host often rewrites what is there
    uint32_t ints = save_and_disable_interrupts();
    flash_range_erase(DISK_FLASH_OFFSET + off, BLOCK);
    flash_range_program(DISK_FLASH_OFFSET + off, cache, BLOCK);
    restore_interrupts(ints);
}

void flashdisk_read(uint32_t sector, uint8_t *buf, uint32_t count) {
    for (; count; count--, sector++, buf += DISK_SECTOR) {
        if (sector >= DISK_SECTORS) { memset(buf, 0, DISK_SECTOR); continue; }
        if ((int32_t)(sector / SPB) == cache_blk)
            memcpy(buf, cache + (sector % SPB) * DISK_SECTOR, DISK_SECTOR);
        else
            memcpy(buf, flash_ptr(sector * DISK_SECTOR), DISK_SECTOR);
    }
}

void flashdisk_write(uint32_t sector, const uint8_t *buf, uint32_t count) {
    for (; count; count--, sector++, buf += DISK_SECTOR) {
        if (sector >= DISK_SECTORS) continue;
        int32_t blk = (int32_t)(sector / SPB);
        if (blk != cache_blk) {
            flashdisk_flush();
            memcpy(cache, flash_ptr((uint32_t)blk * BLOCK), BLOCK);
            cache_blk = blk;
        }
        memcpy(cache + (sector % SPB) * DISK_SECTOR, buf, DISK_SECTOR);
        cache_dirty = true;
    }
}

void flashdisk_poll(uint32_t now_ms) {
    if (cache_dirty && now_ms - disk_last_write_ms > 150) flashdisk_flush();
}

// ---- formatter: a FAT12 "superfloppy" (no partition table), 1 sector per cluster ----

#define RESERVED   1
#define NFATS      2
#define ROOT_ENTS  256
#define ROOT_SECS  (ROOT_ENTS * 32 / DISK_SECTOR)

static const char README[] =
    "2068 PicoCart\r\n"
    "=============\r\n"
    "\r\n"
    "Copy a TS2068 dock cartridge onto this drive: a .DCK file, or a raw\r\n"
    "ROS image (.BIN or .ROM). Eject the drive, then switch the 2068 off and\r\n"
    "on again: the cart serves the file you copied last.\r\n"
    "\r\n"
    "A .BIN loads at $8000 (AROS) unless its name says otherwise, for\r\n"
    "example GAME@0000.BIN for an LROS image at $0000.\r\n"
    "\r\n"
    "Hold MENU while powering the 2068 to start it without the cart.\r\n";

_Static_assert(sizeof README - 1 <= DISK_SECTOR, "README fits one cluster");

static void put16(uint8_t *p, uint16_t v) { p[0] = v; p[1] = v >> 8; }
static void put32(uint8_t *p, uint32_t v) { put16(p, v); put16(p + 2, v >> 16); }

static uint16_t fat_sectors(void) {
    for (uint16_t n = 1;; n++) {
        uint32_t clusters = DISK_SECTORS - RESERVED - NFATS * n - ROOT_SECS;
        if ((clusters + 2) * 3 / 2 + 1 <= (uint32_t)n * DISK_SECTOR) return n;
    }
}

static void dirent(uint8_t *e, const char name[11], uint8_t attr, uint16_t cluster, uint32_t size) {
    static const uint16_t date = ((2026 - 1980) << 9) | (11 << 5) | 20, time = 12 << 11;
    memcpy(e, name, 11);
    e[11] = attr;
    put16(e + 14, time); put16(e + 16, date); put16(e + 18, date);
    put16(e + 22, time); put16(e + 24, date);
    put16(e + 26, cluster); put32(e + 28, size);
}

// A long-name entry run (last part first), then the 8.3 entry. Returns entries used.
static int lfn_dirent(uint8_t *e, const char *lname, const char sname[11], uint8_t attr) {
    static const uint8_t pos[13] = {1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30};
    int len = (int)strlen(lname), parts = (len + 12) / 13;
    uint8_t sum = 0;
    for (int i = 0; i < 11; i++) sum = (uint8_t)(((sum & 1) << 7) + (sum >> 1) + (uint8_t)sname[i]);
    for (int p = parts; p >= 1; p--, e += 32) {
        memset(e, 0, 32);
        e[0] = (uint8_t)(p | (p == parts ? 0x40 : 0));
        e[11] = 0x0f; e[13] = sum;
        for (int i = 0; i < 13; i++) {
            int c = (p - 1) * 13 + i;
            put16(e + pos[i], c < len ? (uint8_t)lname[c] : c == len ? 0x0000 : 0xffff);
        }
    }
    dirent(e, sname, attr, 0, 0);
    return parts + 1;
}

static void format(void) {
    uint8_t s[DISK_SECTOR];
    uint16_t fs = fat_sectors();
    uint32_t root = RESERVED + NFATS * fs, data = root + ROOT_SECS;

    memset(s, 0, sizeof s);                                      // boot sector + BPB
    memcpy(s, "\xeb\x3c\x90" "MSWIN4.1", 11);
    put16(s + 11, DISK_SECTOR); s[13] = 1; put16(s + 14, RESERVED); s[16] = NFATS;
    put16(s + 17, ROOT_ENTS); put16(s + 19, DISK_SECTORS); s[21] = 0xf8; put16(s + 22, fs);
    put16(s + 24, 32); put16(s + 26, 2);
    s[36] = 0x80; s[38] = 0x29; put32(s + 39, 0x20682068);
    memcpy(s + 43, "PICOCART   FAT12   ", 19);
    s[510] = 0x55; s[511] = 0xaa;
    flashdisk_write(0, s, 1);

    for (uint32_t f = 0; f < NFATS; f++)                         // FATs: media, reserved, README = cluster 2
        for (uint32_t i = 0; i < fs; i++) {
            memset(s, 0, sizeof s);
            if (i == 0) memcpy(s, "\xf8\xff\xff\xff\x0f", 5);
            flashdisk_write(RESERVED + f * fs + i, s, 1);
        }

    for (uint32_t i = 0; i < ROOT_SECS; i++) {                   // root directory
        memset(s, 0, sizeof s);
        if (i == 0) {
            uint8_t *e = s;
            dirent(e, "PICOCART   ", 0x08, 0, 0); e += 32;
            dirent(e, "README  TXT", 0x00, 2, sizeof README - 1); e += 32;
            lfn_dirent(e, ".metadata_never_index", "METADA~1   ", 0x02);   // keeps Spotlight off it
        }
        flashdisk_write(root + i, s, 1);
    }

    memset(s, 0, sizeof s);
    memcpy(s, README, sizeof README - 1);
    flashdisk_write(data, s, 1);
    flashdisk_flush();
}

void flashdisk_init(void) {
    const uint8_t *b = flash_ptr(0);
    bool ok = b[510] == 0x55 && b[511] == 0xaa && (b[0] == 0xeb || b[0] == 0xe9) &&
              b[11] == 0x00 && b[12] == 0x02;                    // 512-byte sectors
    if (!ok) format();
}

// ---- USB mass storage callbacks (run from tud_task on core 0) ----

void tud_msc_inquiry_cb(uint8_t lun, uint8_t vendor_id[8], uint8_t product_id[16], uint8_t product_rev[4]) {
    (void)lun;
    memcpy(vendor_id, "PicoCart", 8);
    memcpy(product_id, "2068 Cart Drive ", 16);
    memcpy(product_rev, "1.0 ", 4);
}

bool tud_msc_test_unit_ready_cb(uint8_t lun) {
    if (disk_ejected) {
        tud_msc_set_sense(lun, SCSI_SENSE_NOT_READY, 0x3a, 0x00);   // medium not present
        return false;
    }
    return true;
}

void tud_msc_capacity_cb(uint8_t lun, uint32_t *block_count, uint16_t *block_size) {
    (void)lun;
    *block_count = DISK_SECTORS;
    *block_size = DISK_SECTOR;
}

bool tud_msc_start_stop_cb(uint8_t lun, uint8_t power_condition, bool start, bool load_eject) {
    (void)lun; (void)power_condition;
    if (load_eject) {
        if (start) disk_ejected = false;
        else { flashdisk_flush(); disk_ejected = true; disk_changed = true; }
    }
    return true;
}

int32_t tud_msc_read10_cb(uint8_t lun, uint32_t lba, uint32_t offset, void *buffer, uint32_t bufsize) {
    (void)lun;
    if (offset || bufsize % DISK_SECTOR) return -1;              // EP buffer is a multiple of 512
    flashdisk_read(lba, buffer, bufsize / DISK_SECTOR);
    return (int32_t)bufsize;
}

int32_t tud_msc_write10_cb(uint8_t lun, uint32_t lba, uint32_t offset, uint8_t *buffer, uint32_t bufsize) {
    (void)lun;
    if (offset || bufsize % DISK_SECTOR) return -1;
    flashdisk_write(lba, buffer, bufsize / DISK_SECTOR);
    disk_last_write_ms = to_ms_since_boot(get_absolute_time());
    disk_changed = true;
    return (int32_t)bufsize;
}

int32_t tud_msc_scsi_cb(uint8_t lun, uint8_t const scsi_cmd[16], void *buffer, uint16_t bufsize) {
    (void)buffer; (void)bufsize;
    if (scsi_cmd[0] == 0x35) { flashdisk_flush(); return 0; }    // SYNCHRONIZE CACHE (10)
    tud_msc_set_sense(lun, SCSI_SENSE_ILLEGAL_REQUEST, 0x20, 0x00);
    return -1;
}

// ---- FatFs glue (the loader reads the drive through the same cache) ----

DSTATUS disk_status(BYTE pdrv) { return pdrv ? STA_NOINIT : 0; }
DSTATUS disk_initialize(BYTE pdrv) { return pdrv ? STA_NOINIT : 0; }

DRESULT disk_read(BYTE pdrv, BYTE *buff, LBA_t sector, UINT count) {
    if (pdrv) return RES_PARERR;
    flashdisk_read((uint32_t)sector, buff, count);
    return RES_OK;
}

DRESULT disk_write(BYTE pdrv, const BYTE *buff, LBA_t sector, UINT count) {
    if (pdrv) return RES_PARERR;
    flashdisk_write((uint32_t)sector, buff, count);
    return RES_OK;
}

DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void *buff) {
    if (pdrv) return RES_PARERR;
    switch (cmd) {
    case CTRL_SYNC: flashdisk_flush(); return RES_OK;
    case GET_SECTOR_COUNT: *(LBA_t *)buff = DISK_SECTORS; return RES_OK;
    case GET_SECTOR_SIZE: *(WORD *)buff = DISK_SECTOR; return RES_OK;
    case GET_BLOCK_SIZE: *(DWORD *)buff = SPB; return RES_OK;
    default: return RES_PARERR;
    }
}
