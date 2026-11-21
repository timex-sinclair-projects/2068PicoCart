// The cart drive: a FAT12 volume in on-chip flash, shared by USB mass storage and the loader.
#pragma once
#include <stdbool.h>
#include <stdint.h>

#define DISK_FLASH_OFFSET 0x80000u                 // firmware lives below 512 KB
#define DISK_BYTES        0x170000u                // 1.4 MB, up to the config block
#define DISK_SECTOR       512u
#define DISK_SECTORS      (DISK_BYTES / DISK_SECTOR)
#define CONFIG_FLASH_OFFSET 0x1ff000u              // last 4 KB of flash; the settings log (config.c)
                                                     // uses this sector and the one before it

void flashdisk_init(void);                         // format the drive if it has no FAT boot sector
void flashdisk_read(uint32_t sector, uint8_t *buf, uint32_t count);
void flashdisk_write(uint32_t sector, const uint8_t *buf, uint32_t count);
void flashdisk_flush(void);
void flashdisk_poll(uint32_t now_ms);              // write the cache back once writes go quiet

// set by USB writes and eject; the loader clears them after a rescan
extern volatile bool disk_changed, disk_ejected;
extern volatile uint32_t disk_last_write_ms;
