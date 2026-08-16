/* FAT32 parser over the in-kernel RAM virtual disk.
 * The 16 MiB image is linked into the kernel via kernel/ramdisk.S.
 * Supports: BPB parsing, FAT chain walking, 8.3 directory lookup,
 * file open/read and directory iteration.  Enough for a real FAT32 boot
 * volume; long-name entries are skipped.
 */
#include <kernel/fat32.h>
#include <kernel/klog.h>
#include <kernel/string.h>

extern uint8_t ramdisk_start[];
extern uint8_t ramdisk_end[];

#define FAT32_EOC   0x0ffffff8u
#define FAT32_BAD   0x0ffffff7u
#define FAT32_MASK  0x0fffffffu

struct fat32_fs fat_fs;
static uint32_t rd_bytes;

static uint8_t *rd_byte(uint32_t off)
{
    return &ramdisk_start[off];
}

static uint32_t rd32(uint32_t off)
{
    return *(volatile uint32_t *)rd_byte(off);
}

static uint16_t rd16(uint32_t off)
{
    return *(volatile uint16_t *)rd_byte(off);
}

static void name_83(const char *name, char out[11])
{
    int i;
    for (i = 0; i < 11; i++)
        out[i] = ' ';
    i = 0;
    int o = 0;
    while (name[i] && name[i] != '/' && name[i] != '.' && o < 8) {
        char c = name[i++];
        if (c >= 'a' && c <= 'z')
            c -= 32;
        out[o++] = c;
    }
    if (name[i] == '.')
        i++;
    o = 8;
    while (name[i] && name[i] != '/' && o < 11) {
        char c = name[i++];
        if (c >= 'a' && c <= 'z')
            c -= 32;
        out[o++] = c;
    }
}

static uint32_t cluster_to_byte(uint32_t cluster)
{
    return fat_fs.data_start * fat_fs.bytes_per_sector +
           (cluster - 2) * fat_fs.cluster_bytes;
}

static uint32_t fat_next_cluster(uint32_t cluster)
{
    uint32_t off = fat_fs.fat_start * fat_fs.bytes_per_sector + cluster * 4;
    if (off + 4 > rd_bytes)
        return FAT32_EOC;
    return rd32(off) & FAT32_MASK;
}

int fat32_init(void)
{
    struct fat32_bpb *b = (struct fat32_bpb *)ramdisk_start;

    rd_bytes = (uint32_t)(ramdisk_end - ramdisk_start);
    if (rd_bytes < sizeof(*b)) {
        klog_puts("[qvos] FAT32: ramdisk too small\n");
        return -1;
    }
    if (rd16(510) != 0xaa55 || b->bytes_per_sector != 512 ||
        b->sectors_per_cluster == 0 || b->num_fats == 0) {
        klog_puts("[qvos] FAT32: bad boot sector\n");
        return -1;
    }

    uint32_t total = b->total_sectors16 ? b->total_sectors16 :
                     b->total_sectors32;
    uint32_t spf = b->sectors_per_fat16 ? b->sectors_per_fat16 :
                   b->sectors_per_fat32;

    fat_fs.bytes_per_sector = b->bytes_per_sector;
    fat_fs.sectors_per_cluster = b->sectors_per_cluster;
    fat_fs.cluster_bytes = b->bytes_per_sector * b->sectors_per_cluster;
    fat_fs.reserved_sectors = b->reserved_sectors;
    fat_fs.num_fats = b->num_fats;
    fat_fs.sectors_per_fat = spf;
    fat_fs.fat_start = b->reserved_sectors;
    fat_fs.data_start = b->reserved_sectors + b->num_fats * spf;
    fat_fs.root_cluster = b->root_cluster;
    fat_fs.total_sectors = total;

    if (spf == 0 || fat_fs.root_cluster < 2 ||
        (uint64_t)fat_fs.data_start * 512 > rd_bytes) {
        klog_puts("[qvos] FAT32: not a FAT32 volume (spf=0?)\n");
        return -1;
    }

    klog_puts("[qvos] FAT32 mounted: ");
    klog_hex(rd_bytes);
    klog_puts(" bytes, spc=");
    klog_hex(fat_fs.sectors_per_cluster);
    klog_puts(", root_cluster=");
    klog_hex(fat_fs.root_cluster);
    klog_puts("\n");
    return 0;
}

int fat32_dir_next(struct fat32_dir_iter *it, struct fat32_dirent *de)
{
    uint32_t cluster = it->cluster;

    for (;;) {
        if (cluster < 2 || cluster >= FAT32_EOC)
            return 0;

        uint32_t byte = cluster_to_byte(cluster) +
                        it->sector * fat_fs.bytes_per_sector;
        if (byte + (it->index + 1) * 32 > rd_bytes)
            return 0;

        uint8_t *ent = rd_byte(byte + it->index * 32);
        it->index++;
        if (it->index * 32 >= fat_fs.cluster_bytes) {
            it->index = 0;
            it->sector++;
            if (it->sector >= fat_fs.sectors_per_cluster) {
                it->sector = 0;
                it->cluster = fat_next_cluster(cluster);
                cluster = it->cluster;
            }
        }

        if (ent[0] == 0x00)
            return 0;                 /* end of directory */
        if (ent[0] == 0xe5)
            continue;                 /* deleted */
        if ((ent[11] & 0x3f) == FAT32_ATTR_LFN)
            continue;                 /* long-name entry */

        memcpy(de, ent, sizeof(*de));
        return 1;
    }
}

int fat32_dir_lookup(uint32_t dir_cluster, const char *name,
                     struct fat32_file *out)
{
    char want[11];
    struct fat32_dir_iter it = { dir_cluster, 0, 0 };
    struct fat32_dirent de;

    name_83(name, want);
    while (fat32_dir_next(&it, &de)) {
        if (memcmp(de.name, want, 11) == 0) {
            out->start_cluster = ((uint32_t)de.cluster_hi << 16) |
                                 de.cluster_lo;
            out->size = de.size;
            out->is_dir = (de.attr & FAT32_ATTR_DIRECTORY) != 0;
            return 0;
        }
    }
    return -1;
}

int fat32_readdir(struct fat32_file *f, uint32_t index,
                  struct fat32_dirent *de)
{
    if (!f || !f->is_dir)
        return -1;

    struct fat32_dir_iter it = { f->start_cluster, 0, 0 };
    uint32_t n = 0;
    while (fat32_dir_next(&it, de)) {
        if (n == index)
            return 0;
        n++;
    }
    return -1;   /* end of directory */
}

int fat32_open(const char *path, struct fat32_file *f)
{
    uint32_t dir = fat_fs.root_cluster;
    const char *p = path;

    f->is_dir = 0;
    while (*p == '/')
        p++;
    if (*p == 0) {
        /* The root directory itself. */
        f->start_cluster = fat_fs.root_cluster;
        f->size = 0;
        f->is_dir = 1;
        return 0;
    }

    for (;;) {
        const char *q = p;
        while (*q && *q != '/')
            q++;
        if (q == p)
            return -1;

        char comp[64];
        size_t n = (size_t)(q - p);
        if (n >= sizeof(comp))
            return -1;
        memcpy(comp, p, n);
        comp[n] = 0;

        if (fat32_dir_lookup(dir, comp, f) != 0)
            return -1;

        if (*q == 0)
            return 0;                 /* final component */

        /* Intermediate component must be a directory. */
        struct fat32_dir_iter it = { dir, 0, 0 };
        struct fat32_dirent de;
        char want[11];
        name_83(comp, want);
        int found = 0;
        while (fat32_dir_next(&it, &de)) {
            if (memcmp(de.name, want, 11) == 0 &&
                (de.attr & FAT32_ATTR_DIRECTORY)) {
                dir = ((uint32_t)de.cluster_hi << 16) | de.cluster_lo;
                found = 1;
                break;
            }
        }
        if (!found)
            return -1;
        p = q + 1;
        while (*p == '/')
            p++;
    }
}

int fat32_read(struct fat32_file *f, void *dst, uint32_t offset, size_t n)
{
    if (offset >= f->size)
        return 0;
    if (n > f->size - offset)
        n = f->size - offset;

    uint32_t cluster = f->start_cluster;
    uint32_t cbytes = fat_fs.cluster_bytes;
    uint32_t skip = offset / cbytes;
    uint32_t in_cluster = offset % cbytes;

    for (uint32_t i = 0; i < skip; i++) {
        cluster = fat_next_cluster(cluster);
        if (cluster >= FAT32_EOC)
            return 0;
    }

    uint8_t *d = (uint8_t *)dst;
    size_t done = 0;
    while (done < n) {
        if (cluster < 2 || cluster >= FAT32_EOC)
            break;
        uint32_t byte = cluster_to_byte(cluster) + in_cluster;
        uint32_t chunk = cbytes - in_cluster;
        if (chunk > n - done)
            chunk = (uint32_t)(n - done);
        if (byte + chunk > rd_bytes)
            chunk = rd_bytes - byte;
        memcpy(d + done, rd_byte(byte), chunk);
        done += chunk;
        in_cluster = 0;
        cluster = fat_next_cluster(cluster);
    }
    return (int)done;
}
