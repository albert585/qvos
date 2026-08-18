/* FAT32 parser over pluggable block-device backends.
 *
 * The only volume at boot is the persistent FAT32 on /dev/vda, mounted
 * through fat32_mount().  The kernel image itself carries no root
 * filesystem; kernel and rootfs are fully decoupled.
 *
 * Supports: BPB parsing, FAT chain walking, 8.3 directory lookup,
 * file open/read/write/create/truncate and directory iteration.  Long-name
 * entries are skipped.
 */
#include <kernel/fat32.h>
#include <kernel/klog.h>
#include <kernel/string.h>

#define FAT32_EOC   0x0ffffff8u
#define FAT32_BAD   0x0ffffff7u
#define FAT32_MASK  0x0fffffffu

static void name_83(const char *name, char out[11]);

/* ---- generic backend accessors ---- */

static int fs_read(struct fat32_fs *fs, uint32_t off, void *dst, size_t n)
{
    if (!fs || !fs->read_at)
        return -1;
    if ((uint64_t)off + n > fs->total_bytes)
        return -1;
    return fs->read_at(fs->dev, off, dst, n);
}

static int fs_write(struct fat32_fs *fs, uint32_t off, const void *src, size_t n)
{
    if (!fs || !fs->write_at || !fs->writable)
        return -1;
    if ((uint64_t)off + n > fs->total_bytes)
        return -1;
    return fs->write_at(fs->dev, off, src, n);
}

static uint32_t cluster_to_byte(struct fat32_fs *fs, uint32_t cluster)
{
    return fs->data_start * fs->bytes_per_sector +
           (cluster - 2) * fs->cluster_bytes;
}

static uint32_t fat_next_cluster(struct fat32_fs *fs, uint32_t cluster)
{
    uint32_t off = fs->fat_start * fs->bytes_per_sector + cluster * 4;
    uint32_t v;

    if (off + 4 > fs->total_bytes)
        return FAT32_EOC;
    if (fs_read(fs, off, &v, sizeof(v)) != 0)
        return FAT32_EOC;
    return v & FAT32_MASK;
}

/* Update FAT1 and every FAT copy. */
static void fat_set_entry(struct fat32_fs *fs, uint32_t cluster, uint32_t value)
{
    uint32_t base = fs->fat_start * fs->bytes_per_sector + cluster * 4;
    uint32_t v = value & FAT32_MASK;

    for (uint32_t i = 0; i < fs->num_fats; i++)
        fs_write(fs, base + i * fs->sectors_per_fat * fs->bytes_per_sector,
                 &v, sizeof(v));
}

static uint32_t fat_alloc_cluster(struct fat32_fs *fs)
{
    uint32_t data_clusters =
        (fs->total_sectors - fs->data_start) / fs->sectors_per_cluster;
    uint32_t last = data_clusters ? 2 + data_clusters - 1 : 2;

    for (uint32_t c = 2; c <= last; c++) {
        if ((fat_next_cluster(fs, c) & FAT32_MASK) == 0) {
            fat_set_entry(fs, c, 0x0fffffffu);   /* end of chain */
            return c;
        }
    }
    return 0;
}

static void fat_free_chain(struct fat32_fs *fs, uint32_t start)
{
    while (start >= 2 && start < FAT32_EOC) {
        uint32_t next = fat_next_cluster(fs, start);
        fat_set_entry(fs, start, 0);
        start = next;
    }
}

int fat32_mount(struct fat32_fs *fs, void *dev, uint32_t total_bytes,
                fat32_read_fn read_at, fat32_write_fn write_at, int writable)
{
    uint8_t sector[512];
    struct fat32_bpb *b;

    if (!fs || !read_at || total_bytes < sizeof(sector))
        return -1;
    if (read_at(dev, 0, sector, sizeof(sector)) != 0)
        return -1;
    if (sector[510] != 0x55 || sector[511] != 0xaa)
        return -1;

    b = (struct fat32_bpb *)sector;
    if (b->bytes_per_sector != 512 || b->sectors_per_cluster == 0 ||
        b->num_fats == 0)
        return -1;

    uint32_t total = b->total_sectors16 ? b->total_sectors16 :
                     b->total_sectors32;
    uint32_t spf = b->sectors_per_fat16 ? b->sectors_per_fat16 :
                   b->sectors_per_fat32;
    uint32_t data_start = b->reserved_sectors + b->num_fats * spf;

    if (spf == 0 || b->root_cluster < 2 ||
        (uint64_t)data_start * 512 > total_bytes)
        return -1;

    fs->bytes_per_sector = b->bytes_per_sector;
    fs->sectors_per_cluster = b->sectors_per_cluster;
    fs->cluster_bytes = b->bytes_per_sector * b->sectors_per_cluster;
    fs->reserved_sectors = b->reserved_sectors;
    fs->num_fats = b->num_fats;
    fs->sectors_per_fat = spf;
    fs->fat_start = b->reserved_sectors;
    fs->data_start = data_start;
    fs->root_cluster = b->root_cluster;
    fs->total_sectors = total;
    fs->total_bytes = total_bytes;
    fs->dev = dev;
    fs->read_at = read_at;
    fs->write_at = write_at;
    fs->writable = writable;
    return 0;
}

int fat32_dir_next(struct fat32_fs *fs, struct fat32_dir_iter *it,
                   struct fat32_dirent *de)
{
    uint32_t cluster = it->cluster;

    for (;;) {
        if (cluster < 2 || cluster >= FAT32_EOC)
            return 0;

        uint32_t byte = cluster_to_byte(fs, cluster) +
                        it->sector * fs->bytes_per_sector;
        if (byte + (it->index + 1) * 32 > fs->total_bytes)
            return 0;

        uint8_t ent[32];
        if (fs_read(fs, byte + it->index * 32, ent, sizeof(ent)) != 0)
            return 0;
        it->index++;
        if (it->index * 32 >= fs->cluster_bytes) {
            it->index = 0;
            it->sector++;
            if (it->sector >= fs->sectors_per_cluster) {
                it->sector = 0;
                it->cluster = fat_next_cluster(fs, cluster);
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

/* Raw directory scan.  Returns 1 and fills *out / location on a name
 * match; otherwise 0.  With want_create, remembers the first deleted or
 * end-of-directory slot in *ecl, *esec, *eidx so a new entry can be placed
 * there (caller checks ecl != 0). */
static int dir_scan(struct fat32_fs *fs, uint32_t dir_cluster, const char *name,
                    int want_create, struct fat32_dirent *de,
                    struct fat32_file *out, uint32_t *ecl, uint32_t *esec,
                    uint32_t *eidx)
{
    uint32_t cluster = dir_cluster, sector = 0, index = 0;
    char want[11];

    if (ecl)
        *ecl = 0;
    name_83(name, want);
    for (;;) {
        if (cluster < 2 || cluster >= FAT32_EOC)
            return 0;

        uint32_t byte = cluster_to_byte(fs, cluster) +
                        sector * fs->bytes_per_sector;
        if (byte + (index + 1) * 32 > fs->total_bytes)
            return 0;

        uint8_t ent[32];
        if (fs_read(fs, byte + index * 32, ent, sizeof(ent)) != 0)
            return 0;

        if (ent[0] == 0x00) {          /* free tail slot / end marker */
            if (want_create && ecl && !*ecl) {
                *ecl = cluster;
                *esec = sector;
                *eidx = index;
            }
            return 0;
        }
        if (ent[0] == 0xe5) {          /* deleted slot */
            if (want_create && ecl && !*ecl) {
                *ecl = cluster;
                *esec = sector;
                *eidx = index;
            }
        } else if ((ent[11] & 0x3f) != FAT32_ATTR_LFN &&
                   memcmp(ent, want, 11) == 0) {
            if (de)
                memcpy(de, ent, sizeof(*de));
            out->fs = fs;
            out->start_cluster = ((uint32_t)ent[20] << 16) | ent[26] |
                                 ((uint32_t)ent[27] << 8);
            out->size = *(volatile uint32_t *)(ent + 28);
            out->is_dir = (ent[11] & FAT32_ATTR_DIRECTORY) != 0;
            out->dir_cluster = cluster;
            out->dir_sector = sector;
            out->dir_index = index;
            if (ecl) {
                *ecl = cluster;
                *esec = sector;
                *eidx = index;
            }
            return 1;
        }

        index++;
        if (index * 32 >= fs->cluster_bytes) {
            index = 0;
            sector++;
            if (sector >= fs->sectors_per_cluster) {
                sector = 0;
                cluster = fat_next_cluster(fs, cluster);
            }
        }
    }
}

int fat32_dir_lookup(struct fat32_fs *fs, uint32_t dir_cluster,
                     const char *name, struct fat32_file *out)
{
    return dir_scan(fs, dir_cluster, name, 0, 0, out, 0, 0, 0) == 1 ? 0 : -1;
}

int fat32_readdir(struct fat32_file *f, uint32_t index,
                  struct fat32_dirent *de)
{
    struct fat32_fs *fs = f ? f->fs : 0;

    if (!fs || !f || !f->is_dir)
        return -1;

    struct fat32_dir_iter it = { f->start_cluster, 0, 0 };
    uint32_t n = 0;
    while (fat32_dir_next(fs, &it, de)) {
        if (n == index)
            return 0;
        n++;
    }
    return -1;   /* end of directory */
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

int fat32_open_fs(struct fat32_fs *fs, const char *path, struct fat32_file *f)
{
    uint32_t dir = fs->root_cluster;
    const char *p = path;

    if (!fs || !f)
        return -1;
    f->fs = fs;
    f->is_dir = 0;
    f->dir_cluster = f->dir_sector = f->dir_index = 0;
    while (*p == '/')
        p++;
    if (*p == 0) {
        /* The root directory itself. */
        f->start_cluster = fs->root_cluster;
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

        if (fat32_dir_lookup(fs, dir, comp, f) != 0)
            return -1;

        if (*q == 0)
            return 0;                 /* final component */

        /* Intermediate component must be a directory. */
        struct fat32_dir_iter it = { dir, 0, 0 };
        struct fat32_dirent de;
        char want[11];
        name_83(comp, want);
        int found = 0;
        while (fat32_dir_next(fs, &it, &de)) {
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
    struct fat32_fs *fs = f ? f->fs : 0;

    if (!fs || offset >= f->size)
        return 0;
    if (n > f->size - offset)
        n = f->size - offset;

    uint32_t cluster = f->start_cluster;
    uint32_t cbytes = fs->cluster_bytes;
    uint32_t skip = offset / cbytes;
    uint32_t in_cluster = offset % cbytes;

    for (uint32_t i = 0; i < skip; i++) {
        cluster = fat_next_cluster(fs, cluster);
        if (cluster >= FAT32_EOC)
            return 0;
    }

    uint8_t *d = (uint8_t *)dst;
    size_t done = 0;
    while (done < n) {
        if (cluster < 2 || cluster >= FAT32_EOC)
            break;
        uint32_t byte = cluster_to_byte(fs, cluster) + in_cluster;
        uint32_t chunk = cbytes - in_cluster;
        if (chunk > n - done)
            chunk = (uint32_t)(n - done);
        if (byte + chunk > fs->total_bytes)
            chunk = fs->total_bytes - byte;
        if (fs_read(fs, byte, d + done, chunk) != 0)
            break;
        done += chunk;
        in_cluster = 0;
        cluster = fat_next_cluster(fs, cluster);
    }
    return (int)done;
}

/* ---- write support: in-place data + FAT chain allocation ------------ */

static int dir_entry_read(struct fat32_fs *fs, uint32_t cluster,
                          uint32_t sector, uint32_t index,
                          struct fat32_dirent *de)
{
    if (cluster < 2 || cluster >= FAT32_EOC)
        return -1;
    uint32_t byte = cluster_to_byte(fs, cluster) +
                    sector * fs->bytes_per_sector + index * 32;
    if (byte + 32 > fs->total_bytes)
        return -1;
    return fs_read(fs, byte, de, sizeof(*de));
}

static int dir_entry_write(struct fat32_fs *fs, uint32_t cluster,
                           uint32_t sector, uint32_t index,
                           const struct fat32_dirent *de)
{
    if (cluster < 2 || cluster >= FAT32_EOC)
        return -1;
    uint32_t byte = cluster_to_byte(fs, cluster) +
                    sector * fs->bytes_per_sector + index * 32;
    if (byte + 32 > fs->total_bytes)
        return -1;
    return fs_write(fs, byte, de, sizeof(*de));
}

int fat32_sync_entry(struct fat32_file *f)
{
    struct fat32_fs *fs = f ? f->fs : 0;
    struct fat32_dirent ent;

    if (!fs || !f || f->dir_cluster < 2)
        return 0;
    if (dir_entry_read(fs, f->dir_cluster, f->dir_sector, f->dir_index,
                       &ent) != 0)
        return -1;

    uint32_t start = f->start_cluster;
    ent.cluster_hi = (uint16_t)((start >> 16) & 0xffff);
    ent.cluster_lo = (uint16_t)(start & 0xffff);
    ent.size = f->size;
    return dir_entry_write(fs, f->dir_cluster, f->dir_sector, f->dir_index,
                           &ent);
}

int fat32_write(struct fat32_file *f, const void *src, uint32_t offset,
                size_t n)
{
    struct fat32_fs *fs = f ? f->fs : 0;

    if (!fs || !fs->writable || f->is_dir || offset > f->size)
        return -1;

    uint32_t cbytes = fs->cluster_bytes;
    uint32_t skip = offset / cbytes;
    uint32_t in_cluster = offset % cbytes;
    uint32_t cur = f->start_cluster, prev = 0;

    for (uint32_t i = 0; i < skip; i++) {
        if (cur < 2 || cur >= FAT32_EOC)
            return -1;
        prev = cur;
        cur = fat_next_cluster(fs, cur);
    }

    const uint8_t *s = (const uint8_t *)src;
    size_t done = 0;
    while (done < n) {
        if (cur < 2 || cur >= FAT32_EOC) {
            uint32_t nc = fat_alloc_cluster(fs);
            if (!nc)
                break;                 /* disk full */
            if (f->start_cluster < 2)
                f->start_cluster = nc;
            else if (prev >= 2)
                fat_set_entry(fs, prev, nc);
            cur = nc;
            in_cluster = 0;
        }

        uint32_t byte = cluster_to_byte(fs, cur) + in_cluster;
        uint32_t chunk = cbytes - in_cluster;
        if (chunk > n - done)
            chunk = (uint32_t)(n - done);
        if (byte + chunk > fs->total_bytes)
            chunk = fs->total_bytes - byte;
        if (fs_write(fs, byte, s + done, chunk) != 0)
            break;

        done += chunk;
        prev = cur;
        cur = fat_next_cluster(fs, cur);
        in_cluster = 0;
    }

    if (offset + (uint32_t)done > f->size)
        f->size = offset + (uint32_t)done;
    if (done > 0)
        fat32_sync_entry(f);
    return (int)done;
}

int fat32_truncate(struct fat32_file *f, uint32_t new_size)
{
    struct fat32_fs *fs = f ? f->fs : 0;

    if (!fs || f->is_dir)
        return -1;
    if (new_size > f->size)
        return -1;                     /* only shrink for now */
    if (new_size == f->size)
        return 0;

    if (new_size == 0) {
        fat_free_chain(fs, f->start_cluster);
        f->start_cluster = 0;
    } else {
        uint32_t cbytes = fs->cluster_bytes;
        uint32_t keep = (new_size + cbytes - 1) / cbytes;
        uint32_t cur = f->start_cluster;

        for (uint32_t i = 1; i < keep; i++) {
            if (cur < 2 || cur >= FAT32_EOC)
                return -1;
            cur = fat_next_cluster(fs, cur);
        }
        if (cur < 2 || cur >= FAT32_EOC)
            return -1;
        fat_free_chain(fs, fat_next_cluster(fs, cur));
        fat_set_entry(fs, cur, 0x0fffffffu);
    }

    f->size = new_size;
    return fat32_sync_entry(f);
}

int fat32_create_fs(struct fat32_fs *fs, const char *path,
                    struct fat32_file *out)
{
    const char *name = path, *slash = 0;

    if (!fs || !out)
        return -1;
    out->fs = fs;
    for (const char *p = path; *p; p++)
        if (*p == '/')
            slash = p;
    if (slash) {
        name = slash + 1;
        if (!*name)
            return -1;
    }
    if (!name || !*name)
        return -1;

    /* Parent directory: everything before the last '/', or the root. */
    uint32_t dir_cluster = fs->root_cluster;
    if (slash && slash != path) {
        char parent[128];
        size_t plen = (size_t)(slash - path);
        if (plen >= sizeof(parent))
            return -1;
        memcpy(parent, path, plen);
        parent[plen] = 0;

        struct fat32_file pf;
        if (fat32_open_fs(fs, parent, &pf) != 0 || !pf.is_dir)
            return -1;
        dir_cluster = pf.start_cluster;
    }

    struct fat32_dirent de;
    uint32_t ecl, esec, eidx;
    int found = dir_scan(fs, dir_cluster, name, 1, &de, out,
                         &ecl, &esec, &eidx);
    if (found == 1)
        return -1;                     /* already exists */
    if (ecl < 2)
        return -1;                     /* directory full */

    memset(&de, 0, sizeof(de));
    name_83(name, (char *)de.name);
    de.attr = 0;                       /* regular file */
    de.cluster_hi = 0;
    de.cluster_lo = 0;
    de.size = 0;
    if (dir_entry_write(fs, ecl, esec, eidx, &de) != 0)
        return -1;

    out->start_cluster = 0;
    out->size = 0;
    out->is_dir = 0;
    out->dir_cluster = ecl;
    out->dir_sector = esec;
    out->dir_index = eidx;
    return 0;
}
