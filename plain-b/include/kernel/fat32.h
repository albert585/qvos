#ifndef QVOS_FAT32_H
#define QVOS_FAT32_H

#include <stdint.h>
#include <stddef.h>

#define FAT32_ATTR_READ_ONLY 0x01
#define FAT32_ATTR_DIRECTORY 0x10
#define FAT32_ATTR_LFN       0x0f

struct fat32_bpb {
    uint8_t  jump[3];
    uint8_t  oem[8];
    uint16_t bytes_per_sector;
    uint8_t  sectors_per_cluster;
    uint16_t reserved_sectors;
    uint8_t  num_fats;
    uint16_t root_entries;
    uint16_t total_sectors16;
    uint8_t  media;
    uint16_t sectors_per_fat16;
    uint16_t sectors_per_track;
    uint16_t num_heads;
    uint32_t hidden_sectors;
    uint32_t total_sectors32;
    uint32_t sectors_per_fat32;
    uint16_t ext_flags;
    uint16_t fs_version;
    uint32_t root_cluster;
    uint16_t fs_info;
    uint16_t backup_boot_sector;
    uint8_t  reserved[12];
    uint8_t  drive_number;
    uint8_t  reserved1;
    uint8_t  boot_signature;
    uint32_t volume_id;
    uint8_t  volume_label[11];
    uint8_t  fs_type[8];
} __attribute__((packed));

struct fat32_dirent {
    uint8_t  name[11];
    uint8_t  attr;
    uint8_t  ntres;
    uint8_t  crt_time_tenth;
    uint16_t crt_time;
    uint16_t crt_date;
    uint16_t lst_acc_date;
    uint16_t cluster_hi;
    uint16_t wrt_time;
    uint16_t wrt_date;
    uint16_t cluster_lo;
    uint32_t size;
} __attribute__((packed));

/* Block-device backend used by a FAT32 volume.  All offsets are byte
 * offsets from the start of the volume.  read_at/write_at must transfer
 * exactly n bytes and return 0 on success, -1 on failure. */
typedef int (*fat32_read_fn)(void *dev, uint32_t offset, void *dst, size_t n);
typedef int (*fat32_write_fn)(void *dev, uint32_t offset, const void *src, size_t n);

struct fat32_fs {
    uint16_t bytes_per_sector;
    uint8_t  sectors_per_cluster;
    uint32_t cluster_bytes;
    uint32_t reserved_sectors;
    uint32_t num_fats;
    uint32_t sectors_per_fat;
    uint32_t fat_start;      /* first FAT sector */
    uint32_t data_start;     /* first data sector */
    uint32_t root_cluster;
    uint32_t total_sectors;
    uint32_t total_bytes;    /* backend capacity in bytes */

    void *dev;               /* backend private data, e.g. RAM image pointer */
    fat32_read_fn read_at;
    fat32_write_fn write_at;
    int  writable;
};

struct fat32_file {
    struct fat32_fs *fs;     /* volume this file belongs to */
    uint32_t start_cluster;
    uint32_t size;
    int      is_dir;
    /* Directory entry this file was opened/created through (for updating
     * size + cluster fields after writes).  dir_cluster==0 for root. */
    uint32_t dir_cluster;
    uint32_t dir_sector;
    uint32_t dir_index;
};

struct fat32_dir_iter {
    uint32_t cluster;
    uint32_t sector;
    uint32_t index;       /* entry index within sector */
};

/* Mount a FAT32 volume from an arbitrary block backend.  The backend is
 * probed by reading sector 0; on success fs is filled in and ready to use. */
int  fat32_mount(struct fat32_fs *fs, void *dev, uint32_t total_bytes,
                 fat32_read_fn read_at, fat32_write_fn write_at, int writable);

int  fat32_open_fs(struct fat32_fs *fs, const char *path, struct fat32_file *f);
int  fat32_create_fs(struct fat32_fs *fs, const char *path, struct fat32_file *out);
int  fat32_read(struct fat32_file *f, void *dst, uint32_t offset, size_t n);
int  fat32_write(struct fat32_file *f, const void *src, uint32_t offset,
                 size_t n);
int  fat32_truncate(struct fat32_file *f, uint32_t new_size);
int  fat32_sync_entry(struct fat32_file *f);
int  fat32_dir_next(struct fat32_fs *fs, struct fat32_dir_iter *it,
                    struct fat32_dirent *de);
int  fat32_dir_lookup(struct fat32_fs *fs, uint32_t dir_cluster,
                      const char *name, struct fat32_file *out);
int  fat32_readdir(struct fat32_file *f, uint32_t index,
                   struct fat32_dirent *de);

#endif
