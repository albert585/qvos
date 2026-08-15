/* VFS-lite: a global file table over two file types.
 *   FD_CONSOLE: stdin/stdout/stderr on the NS16550.
 *   FD_FAT:     read-mostly files/directories on the FAT32 RAM disk.
 */
#include <kernel/file.h>
#include <kernel/fat32.h>
#include <kernel/klog.h>
#include <kernel/proc.h>
#include <kernel/spinlock.h>
#include <kernel/uart.h>

#define O_RDONLY 0
#define O_WRONLY 1
#define O_RDWR   2

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
static spinlock_t file_lock;

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
        f->type = FD_NONE;
    }
    spin_unlock_irqrestore(&file_lock, flags);
}

struct file *file_open(const char *path, int flags)
{
    struct fat32_file fat;
    if (fat32_open(path, &fat) != 0)
        return 0;

    struct file *f = file_alloc();
    if (!f)
        return 0;
    f->type = FD_FAT;
    f->fat = fat;
    f->offset = 0;
    f->readable = 1;
    f->writable = (flags & (O_WRONLY | O_RDWR)) != 0;
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
    return -1;   /* FAT32 RAM disk is mounted read-only in this phase */
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
    else if (whence == 2)
        base = (int64_t)f->fat.size;
    else
        return -1;

    int64_t np = base + off;
    if (np < 0)
        return -1;
    if (f->type == FD_FAT && np > (int64_t)f->fat.size)
        np = f->fat.size;
    f->offset = (uint32_t)np;
    return (int)np;
}

int file_stat(struct file *f, uint32_t *size, uint32_t *type)
{
    if (!f)
        return -1;
    if (f->type == FD_FAT) {
        *size = f->fat.size;
        *type = 0;
    } else {
        *size = 0;
        *type = 2;   /* character device */
    }
    return 0;
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
