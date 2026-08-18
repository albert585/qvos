/* VFS-lite: a global file table over three file types.
 *   FD_CONSOLE: stdin/stdout/stderr on the NS16550.
 *   FD_FAT:     read-mostly files/directories on the FAT32 RAM disk.
 *   FD_DEV*:    virtual /dev files (fb0/null/zero), routed before FAT32.
 */
#include <kernel/file.h>
#include <kernel/fat32.h>
#include <kernel/framebuffer.h>
#include <kernel/klog.h>
#include <kernel/proc.h>
#include <kernel/spinlock.h>
#include <kernel/string.h>
#include <kernel/uart.h>
#include <kernel/virtioblk.h>
#include <kernel/virtioinput.h>
#include <kernel/virtiokbd.h>

#define O_RDONLY 0
#define O_WRONLY 1
#define O_RDWR   2
#define O_CREAT  0x0040
#define O_TRUNC  0x0200

#define PIPE_BUF_SIZE 512
#define NPIPE 8

struct pipe {
    char buf[PIPE_BUF_SIZE];
    unsigned head, tail;
    int read_ref, write_ref;
    spinlock_t lock;
};

static struct pipe pipes[NPIPE];
static struct file ftable[FILE_TABLE_SIZE];
static struct mount mounts[MOUNT_MAX];
static spinlock_t file_lock;

/* ---- virtio-blk backend for FAT32 mounts ---- */

static int vda_read_at(void *dev, uint32_t off, void *dst, size_t n)
{
    (void)dev;
    return vda_read(dst, off, n) == (int)n ? 0 : -1;
}

static int vda_write_at(void *dev, uint32_t off, const void *src, size_t n)
{
    (void)dev;
    return vda_write(src, off, n) == (int)n ? 0 : -1;
}

static const char *mount_basename(const char *path)
{
    const char *base = path;

    for (const char *p = path; *p; p++)
        if (*p == '/')
            base = p + 1;
    return base;
}

/* Turn a possibly-relative path into a normalized absolute path.  Handles
 * ".", ".." and repeated slashes.  Returns 0 on success. */
static int path_absolutize(const char *cwd, const char *path,
                           char *out, size_t outsz)
{
    char tmp[256];
    char *comps[64];
    int top = 0;
    size_t n = 0;
    const char *base;
    const char *p;

    if (!path || !out || outsz < 2)
        return -1;
    if (path[0] == '/') {
        base = "/";
        p = path + 1;
    } else {
        base = cwd ? cwd : "/";
        p = path;
    }

    for (const char *s = base; *s && n + 1 < sizeof(tmp); s++)
        tmp[n++] = *s;
    if (n == 0 || tmp[n - 1] != '/')
        tmp[n++] = '/';
    for (const char *s = p; *s && n + 1 < sizeof(tmp); s++)
        tmp[n++] = *s;
    tmp[n] = 0;

    char *s = tmp;
    while (*s) {
        while (*s == '/')
            *s++ = 0;
        if (!*s)
            break;
        char *start = s;
        while (*s && *s != '/')
            s++;
        if (*s) {
            *s = 0;
            s++;
        }
        if (strcmp(start, ".") == 0)
            continue;
        if (strcmp(start, "..") == 0) {
            if (top > 0)
                top--;
            continue;
        }
        if (top < 64)
            comps[top++] = start;
    }

    n = 0;
    out[n++] = '/';
    for (int i = 0; i < top; i++) {
        size_t len = strlen(comps[i]);
        if (n + len + 1 >= outsz)
            return -1;
        if (i > 0)
            out[n++] = '/';
        memcpy(out + n, comps[i], len);
        n += len;
    }
    out[n] = 0;
    return 0;
}

int vfs_chdir(const char *path, char *out, size_t outsz)
{
    struct proc *p = myproc();
    char abs[128];
    struct file *f;
    uint64_t size;
    uint32_t type;

    if (!p)
        return -1;
    if (path_absolutize(p->cwd, path, abs, sizeof(abs)) != 0)
        return -1;
    f = file_open(abs, 0);
    if (!f)
        return -1;
    if (file_stat(f, &size, &type) != 0 || type != 1) {
        file_close(f);
        return -1;
    }
    file_close(f);
    strncpy(out, abs, outsz - 1);
    out[outsz - 1] = 0;
    return 0;
}

/* The volume that backs "/" for this boot.  It is set only by
 * vfs_mount(..., "/") — there is no rootfs embedded in the kernel. */
static struct fat32_fs *root_fs;

/* Find the longest mounted prefix of path.  Falls back to root_fs.
 * *rest is the path relative to the chosen filesystem. */
static struct fat32_fs *mount_resolve(const char *path, const char **rest)
{
    struct mount *best = 0;
    size_t bestlen = 0;

    *rest = path;
    for (int i = 0; i < MOUNT_MAX; i++) {
        if (!mounts[i].used)
            continue;
        size_t len = strlen(mounts[i].path);
        if (len == 1 && mounts[i].path[0] == '/')
            continue;               /* the root mount is root_fs itself */
        if (strncmp(path, mounts[i].path, len) == 0 &&
            (path[len] == '\0' || path[len] == '/')) {
            if (len > bestlen) {
                best = &mounts[i];
                bestlen = len;
            }
        }
    }
    if (best) {
        *rest = path + bestlen;
        if (**rest == '\0')
            *rest = "/";
        return &best->fs;
    }
    return root_fs;
}

int vfs_mount(const char *source, const char *target)
{
    struct mount *m = 0;
    int is_root;

    if (!source || !target || target[0] != '/')
        return -1;
    is_root = strcmp(target, "/") == 0;
    for (int i = 0; i < MOUNT_MAX; i++) {
        if (mounts[i].used && strcmp(mounts[i].path, target) == 0)
            return -1;
        if (!mounts[i].used && !m)
            m = &mounts[i];
    }
    if (!m)
        return -1;
    if (strcmp(source, "/dev/vda") != 0 || !vda_active())
        return -1;
    if (vda_capacity() > 0xffffffffULL)
        return -1;
    if (fat32_mount(&m->fs, 0, (uint32_t)vda_capacity(),
                    vda_read_at, vda_write_at, 1) != 0)
        return -1;
    strncpy(m->path, target, sizeof(m->path) - 1);
    m->path[sizeof(m->path) - 1] = 0;
    m->used = 1;
    if (is_root)
        root_fs = &m->fs;   /* switch the whole VFS root to this volume */
    return 0;
}

static void pipe_init(void)
{
    for (int i = 0; i < NPIPE; i++) {
        pipes[i].head = pipes[i].tail = 0;
        pipes[i].read_ref = pipes[i].write_ref = 0;
        pipes[i].lock.locked = 0;
    }
}

void file_init(void)
{
    pipe_init();
    for (int i = 0; i < FILE_TABLE_SIZE; i++)
        ftable[i].type = FD_NONE;
    for (int i = 0; i < MOUNT_MAX; i++)
        mounts[i].used = 0;

    /* Permanent console descriptors 0/1/2. */
    ftable[0].type = FD_CONSOLE;
    ftable[0].ref = 1;
    ftable[0].readable = 1;
    ftable[0].writable = 0;
    ftable[0].offset = 0;

    ftable[1].type = FD_CONSOLE;
    ftable[1].ref = 1;
    ftable[1].readable = 0;
    ftable[1].writable = 1;
    ftable[1].offset = 0;

    ftable[2].type = FD_CONSOLE;
    ftable[2].ref = 1;
    ftable[2].readable = 0;
    ftable[2].writable = 1;
    ftable[2].offset = 0;
}

struct file *file_console(int idx)
{
    if (idx < 0 || idx > 2)
        return 0;
    return &ftable[idx];
}

struct file *file_alloc(void)
{
    uint64_t flags = spin_lock_irqsave(&file_lock);
    struct file *f = 0;
    for (int i = 0; i < FILE_TABLE_SIZE; i++) {
        if (ftable[i].type == FD_NONE) {
            f = &ftable[i];
            f->type = FD_FAT;
            f->ref = 1;
            f->readable = 1;
            f->writable = 0;
            f->offset = 0;
            f->fat.start_cluster = 0;
            f->fat.size = 0;
            f->pipe = 0;
            break;
        }
    }
    spin_unlock_irqrestore(&file_lock, flags);
    return f;
}

/* ---- tiny devfs: exact-match virtual paths ---- */

struct dev_entry {
    const char *path;
    enum file_type type;
};

static const struct dev_entry dev_table[] = {
    { "/dev",       FD_DEVDIR },
    { "/dev/fb0",   FD_DEVFB },
    { "/dev/vda",   FD_DEVVDA },
    { "/dev/mouse", FD_DEVMOUSE },
    { "/dev/kbd",   FD_DEVKBD },
    { "/dev/null",  FD_DEVNULL },
    { "/dev/zero",  FD_DEVZERO },
};

static const struct {
    const char *name;
    uint32_t type;
} dev_names[] = {
    { "fb0",  2 },   /* character device */
    { "vda",  3 },   /* block device */
    { "mouse", 2 },  /* character device */
    { "kbd",   2 },  /* character device */
    { "null", 2 },
    { "zero", 2 },
};

static struct file *dev_open(const char *path, int flags)
{
    enum file_type type = FD_NONE;

    for (size_t i = 0; i < sizeof(dev_table) / sizeof(dev_table[0]); i++) {
        if (strcmp(path, dev_table[i].path) == 0) {
            type = dev_table[i].type;
            break;
        }
    }
    if (type == FD_NONE)
        return 0;
    if (type == FD_DEVFB && !fb_active())
        return 0;
    if (type == FD_DEVVDA && !vda_active())
        return 0;
    if (type == FD_DEVMOUSE && !mouse_active())
        return 0;
    if (type == FD_DEVKBD && !kbd_active())
        return 0;

    struct file *f = file_alloc();
    if (!f)
        return 0;
    f->type = type;
    f->readable = 1;
    f->writable = (type != FD_DEVDIR) &&
                  (flags & (O_WRONLY | O_RDWR)) != 0;
    f->offset = 0;
    return f;
}

struct file *file_dup(struct file *f)
{
    if (!f)
        return 0;
    uint64_t flags = spin_lock_irqsave(&file_lock);
    f->ref++;
    if (f->type == FD_PIPE && f->pipe) {
        struct pipe *p = (struct pipe *)f->pipe;
        if (f->readable)
            p->read_ref++;
        if (f->writable)
            p->write_ref++;
    }
    spin_unlock_irqrestore(&file_lock, flags);
    return f;
}

void file_close(struct file *f)
{
    if (!f)
        return;
    uint64_t flags = spin_lock_irqsave(&file_lock);
    if (f->type == FD_CONSOLE) {
        f->ref = 1;      /* stdio never disappears */
    } else if (f->type == FD_PIPE && f->pipe) {
        struct pipe *p = (struct pipe *)f->pipe;
        if (f->readable && p->read_ref > 0)
            p->read_ref--;
        if (f->writable && p->write_ref > 0)
            p->write_ref--;
        if (p->read_ref == 0 && p->write_ref == 0) {
            p->head = p->tail = 0;
            f->pipe = 0;
            f->type = FD_NONE;
        } else if (--f->ref <= 0) {
            f->type = FD_NONE;
        }
    } else if (f->ref > 0 && --f->ref == 0) {
        if (f->type == FD_FAT)
            fat32_sync_entry(&f->fat);
        f->type = FD_NONE;
    }
    spin_unlock_irqrestore(&file_lock, flags);
}

struct file *file_open(const char *path, int flags)
{
    char abspath[128];
    const char *rpath = path;
    struct proc *p = myproc();

    if (path && path[0] != '/' && p) {
        if (path_absolutize(p->cwd, path, abspath, sizeof(abspath)) != 0)
            return 0;
        rpath = abspath;
    }

    struct file *dev = dev_open(rpath, flags);
    if (dev)
        return dev;

    const char *rest;
    struct fat32_fs *fs = mount_resolve(rpath, &rest);
    struct fat32_file fat;
    if (fat32_open_fs(fs, rest, &fat) != 0) {
        if (!(flags & O_CREAT))
            return 0;
        if (fat32_create_fs(fs, rest, &fat) != 0)
            return 0;
    } else if ((flags & O_CREAT) && (flags & 0x0800)) {  /* O_EXCL */
        return 0;
    }

    struct file *f = file_alloc();
    if (!f)
        return 0;
    f->type = FD_FAT;
    f->fat = fat;
    f->offset = 0;
    f->readable = (flags & O_WRONLY) == 0;
    f->writable = (flags & (O_WRONLY | O_RDWR)) != 0;
    if ((flags & O_TRUNC) && f->writable && !f->fat.is_dir)
        fat32_truncate(&f->fat, 0);
    return f;
}

int file_read(struct file *f, void *dst, size_t n)
{
    if (!f || !f->readable)
        return -1;
    if (f->type == FD_CONSOLE) {
        int c = uart_getc_blocking();
        ((uint8_t *)dst)[0] = (uint8_t)c;
        return 1;
    }
    if (f->type == FD_FAT) {
        int got = fat32_read(&f->fat, dst, f->offset, n);
        if (got > 0)
            f->offset += (uint32_t)got;
        return got;
    }
    if (f->type == FD_DEVFB) {
        if (!fb_active())
            return -1;
        uint32_t size = fb_size_bytes();
        if (f->offset >= size)
            return 0;                       /* EOF */
        size_t want = n;
        if ((uint64_t)want > (uint64_t)(size - f->offset))
            want = size - f->offset;
        memcpy(dst, (void *)(fb_paddr() + f->offset), want);
        f->offset += (uint32_t)want;
        return (int)want;
    }
    if (f->type == FD_DEVVDA) {
        int got = vda_read(dst, f->offset, n);
        if (got > 0)
            f->offset += (uint32_t)got;
        return got;
    }
    if (f->type == FD_DEVMOUSE) {
        int events = mouse_read_events(dst,
                                       n / sizeof(struct qvos_input_event));
        return events * (int)sizeof(struct qvos_input_event);
    }
    if (f->type == FD_DEVKBD) {
        int events = kbd_read_events(dst,
                                     n / sizeof(struct qvos_input_event));
        return events * (int)sizeof(struct qvos_input_event);
    }
    if (f->type == FD_DEVNULL)
        return 0;                           /* always EOF */
    if (f->type == FD_DEVZERO) {
        memset(dst, 0, n);
        f->offset += (uint32_t)n;
        return (int)n;
    }
    if (f->type == FD_PIPE && f->pipe) {
        struct pipe *p = (struct pipe *)f->pipe;
        uint64_t flags = spin_lock_irqsave(&p->lock);
        size_t avail = p->tail - p->head;
        size_t want = n < avail ? n : avail;
        for (size_t i = 0; i < want; i++)
            ((uint8_t *)dst)[i] = p->buf[p->head++ % PIPE_BUF_SIZE];
        spin_unlock_irqrestore(&p->lock, flags);
        return (int)want;
    }
    return -1;
}

int file_write(struct file *f, const void *src, size_t n)
{
    if (!f || !f->writable)
        return -1;
    if (f->type == FD_CONSOLE) {
        klog_write((const char *)src, n);
        f->offset += (uint32_t)n;
        return (int)n;
    }
    if (f->type == FD_PIPE && f->pipe) {
        struct pipe *p = (struct pipe *)f->pipe;
        uint64_t flags = spin_lock_irqsave(&p->lock);
        size_t space = PIPE_BUF_SIZE - (p->tail - p->head);
        size_t want = n < space ? n : space;
        for (size_t i = 0; i < want; i++)
            p->buf[p->tail++ % PIPE_BUF_SIZE] = ((const uint8_t *)src)[i];
        spin_unlock_irqrestore(&p->lock, flags);
        return (int)want;
    }
    if (f->type == FD_DEVFB) {
        if (!fb_active())
            return -1;
        uint32_t size = fb_size_bytes();
        if (f->offset >= size)
            return 0;
        size_t want = n;
        if ((uint64_t)want > (uint64_t)(size - f->offset))
            want = size - f->offset;
        memcpy((void *)(fb_paddr() + f->offset), src, want);
        f->offset += (uint32_t)want;
        if (want)
            fb_flush();   /* raw pixels are invisible until flushed */
        return (int)want;
    }
    if (f->type == FD_DEVVDA) {
        int wrote = vda_write(src, f->offset, n);
        if (wrote > 0)
            f->offset += (uint32_t)wrote;
        return wrote;
    }
    if (f->type == FD_DEVNULL || f->type == FD_DEVZERO) {
        f->offset += (uint32_t)n;
        return (int)n;
    }
    if (f->type == FD_FAT) {
        int wrote = fat32_write(&f->fat, src, f->offset, n);
        if (wrote > 0) {
            f->offset += (uint32_t)wrote;
            fat32_sync_entry(&f->fat);
        }
        return wrote;
    }
    return -1;   /* unknown write target */
}

int file_lseek(struct file *f, int64_t off, int whence)
{
    if (!f)
        return -1;
    int64_t base = 0;
    if (whence == 0)
        base = 0;
    else if (whence == 1)
        base = (int64_t)f->offset;
    else if (whence == 2) {
        if (f->type == FD_FAT)
            base = (int64_t)f->fat.size;
        else if (f->type == FD_DEVFB)
            base = (int64_t)fb_size_bytes();
        else if (f->type == FD_DEVVDA) {
            uint64_t cap = vda_capacity();
            if (cap > 0xffffffffULL)     /* struct file.offset is 32-bit */
                cap = 0xffffffffULL;
            base = (int64_t)cap;
        } else
            base = 0;
    } else
        return -1;

    int64_t np = base + off;
    if (np < 0)
        return -1;
    if (f->type == FD_FAT && np > (int64_t)f->fat.size)
        np = f->fat.size;
    if (f->type == FD_DEVFB && np > (int64_t)fb_size_bytes())
        np = (int64_t)fb_size_bytes();
    if (f->type == FD_DEVVDA) {
        uint64_t cap = vda_capacity();
        if (cap > 0xffffffffULL)
            cap = 0xffffffffULL;
        if (np > (int64_t)cap)
            np = (int64_t)cap;
    }
    f->offset = (uint32_t)np;
    return (int)np;
}

int file_stat(struct file *f, uint64_t *size, uint32_t *type)
{
    if (!f)
        return -1;
    if (f->type == FD_FAT) {
        *size = f->fat.size;
        *type = f->fat.is_dir ? 1 : 0;
    } else if (f->type == FD_DEVDIR) {
        *size = 0;
        *type = 1;   /* directory */
    } else if (f->type == FD_DEVFB) {
        *size = fb_size_bytes();
        *type = 2;   /* character device */
    } else if (f->type == FD_DEVVDA) {
        *size = vda_capacity();
        *type = 3;   /* block device */
    } else {
        *size = 0;
        *type = 2;   /* character device */
    }
    return 0;
}

/* Convert a FAT 8.3 dirent name into "NAME.EXT" (trimmed, no spaces). */
static void fat_name_display(const uint8_t name83[11], char *out, size_t max)
{
    size_t n = 0;
    for (int i = 0; i < 8 && name83[i] != ' '; i++)
        if (n + 1 < max)
            out[n++] = (char)name83[i];
    if (name83[8] != ' ') {
        if (n + 1 < max)
            out[n++] = '.';
        for (int i = 8; i < 11 && name83[i] != ' '; i++)
            if (n + 1 < max)
                out[n++] = (char)name83[i];
    }
    out[n] = 0;
}

/* Count real FAT directory entries (skips deleted/LFN, like fat32_readdir). */
static uint32_t fat_dir_entry_count(struct fat32_file *f)
{
    struct fat32_dirent raw;
    uint32_t count = 0;
    while (fat32_readdir(f, count, &raw) == 0)
        count++;
    return count;
}

int file_readdir(struct file *f, uint32_t index, struct qvos_dirent *de)
{
    if (!f || !de)
        return -1;

    if (f->type == FD_DEVDIR) {
        if (index >= sizeof(dev_names) / sizeof(dev_names[0]))
            return -1;
        size_t n = strlen(dev_names[index].name);
        de->d_ino = index + 1;
        de->d_type = dev_names[index].type;
        de->d_namlen = (uint32_t)n;
        memcpy(de->d_name, dev_names[index].name, n);
        de->d_name[n] = 0;
        return 0;
    }

    if (f->type == FD_FAT && f->fat.is_dir) {
        struct fat32_dirent raw;
        if (fat32_readdir(&f->fat, index, &raw) == 0) {
            de->d_ino = ((uint32_t)raw.cluster_hi << 16) | raw.cluster_lo;
            de->d_type = (raw.attr & FAT32_ATTR_DIRECTORY) ? 1 : 0;
            fat_name_display(raw.name, de->d_name, sizeof(de->d_name));
            de->d_namlen = (uint32_t)strlen(de->d_name);
            return 0;
        }

        /* The devfs and mounted volumes are virtual and have no FAT
         * dirents: append synthetic entries after the real root entries. */
        if (f->fat.fs == root_fs && f->fat.start_cluster == root_fs->root_cluster) {
            uint32_t base = fat_dir_entry_count(&f->fat);
            uint32_t idx = index - base;
            if (idx == 0) {
                de->d_ino = 0x00de0000;
                de->d_type = 1;
                de->d_namlen = 3;
                memcpy(de->d_name, "dev", 4);
                return 0;
            }
            idx--;
            int mcount = 0;
            for (int i = 0; i < MOUNT_MAX; i++) {
                if (!mounts[i].used || strcmp(mounts[i].path, "/") == 0)
                    continue;
                if (mcount == (int)idx) {
                    const char *base_name = mount_basename(mounts[i].path);
                    size_t n = strlen(base_name);
                    de->d_ino = 0x00de0000 + i + 1;
                    de->d_type = 1;
                    de->d_namlen = (uint32_t)n;
                    memcpy(de->d_name, base_name, n);
                    de->d_name[n] = 0;
                    return 0;
                }
                mcount++;
            }
        }
        return -1;
    }

    return -1;   /* not a directory */
}

int pipe_alloc(int fds[2])
{
    struct pipe *p = 0;
    for (int i = 0; i < NPIPE; i++) {
        if (pipes[i].read_ref == 0 && pipes[i].write_ref == 0) {
            p = &pipes[i];
            break;
        }
    }
    if (!p)
        return -1;

    struct file *rf = file_alloc();
    struct file *wf = file_alloc();
    if (!rf || !wf) {
        file_close(rf);
        file_close(wf);
        return -1;
    }

    rf->type = FD_PIPE;
    rf->readable = 1;
    rf->writable = 0;
    rf->pipe = p;
    wf->type = FD_PIPE;
    wf->readable = 0;
    wf->writable = 1;
    wf->pipe = p;
    p->head = p->tail = 0;
    p->read_ref = 1;
    p->write_ref = 1;

    int rfd = fd_alloc(rf);
    if (rfd < 0) {
        file_close(rf);
        file_close(wf);
        return -1;
    }
    int wfd = fd_alloc(wf);
    if (wfd < 0) {
        fd_close(rfd);
        file_close(wf);
        return -1;
    }
    fds[0] = rfd;
    fds[1] = wfd;
    return 0;
}

int fd_alloc(struct file *f)
{
    struct proc *p = myproc();
    if (!p)
        return -1;
    for (int fd = 0; fd < NOFILE; fd++) {
        if (p->ofile[fd] == 0) {
            p->ofile[fd] = f;
            return fd;
        }
    }
    return -1;
}

struct file *fd_get(int fd)
{
    struct proc *p = myproc();
    if (!p || fd < 0 || fd >= NOFILE)
        return 0;
    return (struct file *)p->ofile[fd];
}

void fd_close(int fd)
{
    struct proc *p = myproc();
    if (!p || fd < 0 || fd >= NOFILE)
        return;
    struct file *f = (struct file *)p->ofile[fd];
    p->ofile[fd] = 0;
    file_close(f);
}
