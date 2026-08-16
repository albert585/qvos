/* POSIX-subset syscall dispatch: process + console + FAT32 file syscalls.
 */
#include <kernel/file.h>
#include <kernel/framebuffer.h>
#include <kernel/kalloc.h>
#include <kernel/klog.h>
#include <kernel/panic.h>
#include <kernel/proc.h>
#include <kernel/sbi.h>
#include <kernel/string.h>
#include <kernel/syscall.h>
#include <kernel/vm.h>

#define MAX_ARGV 64
#define MAX_ARG_LEN 128

struct qvos_stat {
    uint64_t st_size;
    uint32_t st_mode;
};

static uint64_t arg(struct trapframe *tf, int n)
{
    return tf->gpr[10 + n];
}

static int copyout(pagetable_t pg, const void *src, uint64_t dstva, size_t len)
{
    const uint8_t *s = (const uint8_t *)src;

    while (len > 0) {
        uint64_t pa = vm_translate(pg, dstva);
        if (!pa)
            return -1;
        size_t n = PAGE_SIZE - (dstva & PAGE_MASK);
        if (n > len)
            n = len;
        for (size_t i = 0; i < n; i++)
            ((volatile uint8_t *)pa)[i] = s[i];
        s += n;
        dstva += n;
        len -= n;
    }
    return 0;
}

static long sys_write(struct trapframe *tf)
{
    int fd = (int)arg(tf, 0);
    uint64_t uva = arg(tf, 1);
    size_t n = (size_t)arg(tf, 2);
    struct file *f = fd_get(fd);

    if (!f)
        return -1;
    if (n > 4096)
        n = 4096;
    char *buf = kmalloc(n);
    if (!buf)
        return -1;
    if (copyin(myproc()->pg, buf, uva, n) != 0) {
        kfree(buf);
        return -1;
    }
    long r = file_write(f, buf, n);
    kfree(buf);
    return r;
}

static long sys_read(struct trapframe *tf)
{
    int fd = (int)arg(tf, 0);
    uint64_t uva = arg(tf, 1);
    size_t n = (size_t)arg(tf, 2);
    struct file *f = fd_get(fd);

    if (!f || n == 0)
        return -1;
    if (n > 4096)
        n = 4096;
    char *buf = kmalloc(n);
    if (!buf)
        return -1;
    long r = file_read(f, buf, n);
    if (r < 0) {
        kfree(buf);
        return -1;
    }

    /* Write the bytes back into the user buffer. */
    uint64_t pa;
    for (long i = 0; i < r; i++) {
        pa = vm_translate(myproc()->pg, uva + (uint64_t)i);
        if (!pa) {
            kfree(buf);
            return -1;
        }
        ((volatile uint8_t *)pa)[0] = buf[i];
    }
    kfree(buf);
    return r;
}

static long sys_open(struct trapframe *tf)
{
    uint64_t path_uva = arg(tf, 0);
    int flags = (int)arg(tf, 1);
    char path[128];

    if (copyinstr(myproc()->pg, path, path_uva, sizeof(path)) != 0)
        return -1;
    struct file *f = file_open(path, flags);
    if (!f)
        return -1;
    int fd = fd_alloc(f);
    if (fd < 0) {
        file_close(f);
        return -1;
    }
    return fd;
}

static long sys_close(struct trapframe *tf)
{
    int fd = (int)arg(tf, 0);
    struct file *f = fd_get(fd);
    if (!f)
        return -1;
    fd_close(fd);
    return 0;
}

static long sys_lseek(struct trapframe *tf)
{
    int fd = (int)arg(tf, 0);
    int64_t off = (int64_t)arg(tf, 1);
    int whence = (int)arg(tf, 2);
    return (long)file_lseek(fd_get(fd), off, whence);
}

static long sys_fstat(struct trapframe *tf)
{
    int fd = (int)arg(tf, 0);
    uint64_t uva = arg(tf, 1);
    struct qvos_stat st;

    if (file_stat(fd_get(fd), &st.st_size, &st.st_mode) != 0)
        return -1;
    uint64_t pa = vm_translate(myproc()->pg, uva);
    if (!pa)
        return -1;
    for (size_t i = 0; i < sizeof(st); i++)
        ((volatile uint8_t *)pa)[i] = ((uint8_t *)&st)[i];
    return 0;
}

static long sys_dup(struct trapframe *tf)
{
    int fd = (int)arg(tf, 0);
    struct file *f = fd_get(fd);
    if (!f)
        return -1;
    return (long)fd_alloc(file_dup(f));
}

static long sys_pipe(struct trapframe *tf)
{
    uint64_t uva = arg(tf, 0);
    int fds[2];

    if (pipe_alloc(fds) != 0)
        return -1;
    for (int i = 0; i < 2; i++) {
        uint64_t pa = vm_translate(myproc()->pg, uva + (uint64_t)i * 4);
        if (!pa) {
            fd_close(fds[0]);
            fd_close(fds[1]);
            return -1;
        }
        *(volatile uint32_t *)pa = (uint32_t)fds[i];
    }
    return 0;
}

static long sys_getdents(struct trapframe *tf)
{
    int fd = (int)arg(tf, 0);
    uint64_t uva = arg(tf, 1);
    uint32_t max = (uint32_t)arg(tf, 2);
    struct file *f = fd_get(fd);
    struct qvos_dirent *ents;
    long count = 0;

    if (!f || !uva)
        return -1;
    if (max == 0)
        return 0;
    if (max > 64)
        max = 64;

    ents = kmalloc((size_t)max * sizeof(*ents));
    if (!ents)
        return -1;

    for (uint32_t i = 0; i < max; i++) {
        if (file_readdir(f, f->offset + i, &ents[i]) != 0)
            break;
        count++;
    }
    for (long i = 0; i < count; i++) {
        if (copyout(myproc()->pg, &ents[i],
                    uva + (uint64_t)i * sizeof(*ents), sizeof(*ents)) != 0) {
            kfree(ents);
            return -1;
        }
    }
    if (count > 0)
        f->offset += (uint32_t)count;
    kfree(ents);
    return count;
}

static long sys_execve(struct trapframe *tf)
{
    uint64_t path_uva = arg(tf, 0);
    uint64_t argv_uva = arg(tf, 1);
    char path[128];
    char **kargv;
    char *strbuf;

    if (copyinstr(myproc()->pg, path, path_uva, sizeof(path)) != 0)
        return -1;

    kargv = kzalloc((MAX_ARGV + 1) * sizeof(char *));
    strbuf = kmalloc(MAX_ARGV * MAX_ARG_LEN);
    if (!kargv || !strbuf) {
        kfree(kargv);
        kfree(strbuf);
        return -1;
    }

    int argc = 0;
    if (argv_uva) {
        for (argc = 0; argc < MAX_ARGV; argc++) {
            uint64_t ptr_uva = argv_uva + (uint64_t)argc * 8;
            uint64_t pa = vm_translate(myproc()->pg, ptr_uva);
            if (!pa)
                break;
            uint64_t str_uva = *(volatile uint64_t *)pa;
            if (!str_uva)
                break;
            if (copyinstr(myproc()->pg, strbuf + argc * MAX_ARG_LEN,
                          str_uva, MAX_ARG_LEN) != 0)
                break;
            kargv[argc] = strbuf + argc * MAX_ARG_LEN;
        }
    }
    kargv[argc] = 0;

    long r = proc_exec(path, kargv);
    kfree(strbuf);
    kfree(kargv);
    return r;
}

static long sys_fork(void)
{
    return (long)proc_fork();
}

static long sys_getpid(void)
{
    return (long)myproc()->pid;
}

static long sys_getppid(void)
{
    return (long)myproc()->ppid;
}

static long sys_yield(void)
{
    proc_yield();
    return 0;
}

static long sys_waitpid(struct trapframe *tf)
{
    int pid = (int)arg(tf, 0);
    uint64_t status_uva = arg(tf, 1);
    int status = 0;

    int found = proc_waitpid(pid, &status, 0);
    if (found > 0 && status_uva) {
        uint64_t pa = vm_translate(myproc()->pg, status_uva);
        if (pa)
            *(volatile uint32_t *)pa = (uint32_t)status;
    }
    return (long)found;
}

static long sys_sbrk(struct trapframe *tf)
{
    struct proc *p = myproc();
    int64_t n = (int64_t)arg(tf, 0);
    uint64_t old = p->sz;

    if (n == 0)
        return (long)old;
    if (n > 0) {
        uint64_t newsz = old + (uint64_t)n;
        if (uvmalloc(p->pg, old, newsz,
                     PTE_U | PTE_R | PTE_W | PTE_A | PTE_D) < 0)
            return -1;
        p->sz = newsz;
    } else {
        uint64_t newsz = old - (uint64_t)(-n);
        if (newsz < 0x40000000UL)
            newsz = 0x40000000UL;
        if (uvmdealloc(p->pg, old, newsz) < 0)
            return -1;
        p->sz = newsz;
    }
    return (long)old;
}

static long sys_nanosleep(void)
{
    extern void *nanosleep_chan;
    proc_sleep(&nanosleep_chan);
    return 0;
}

static long sys_reboot(struct trapframe *tf)
{
    uint64_t how = arg(tf, 0);
    if ((uint32_t)how == 0xdeadbeefUL)
        panic("panic syscall invoked from userspace", 0xdeadbeefUL, 0,
              tf->sepc);
    if ((uint32_t)how == 0x4321fedcUL) {
        sbi_system_shutdown();   /* Linux-style RB_POWER_OFF magic */
        return -1;               /* not reached on real firmware */
    }
    sbi_system_reset(1, 0);   /* cold reboot */
    return -1;
}

/* ---- virtio-gpu framebuffer syscalls ---- */

static long sys_fbmap(void)
{
    struct proc *p = myproc();
    uint32_t size;

    if (!p || !p->pg || !fb_active())
        return -1;
    size = fb_size_bytes();
    if (size == 0 || size > FB_USER_VA_SIZE)
        return -1;

    /* Share the GPU backing store directly with the process: U/RW, no X.
     * Repeated calls just re-map the same pages and are idempotent. */
    if (vm_map(p->pg, FB_USER_VA, fb_paddr(), size,
               PTE_U | PTE_R | PTE_W | PTE_A | PTE_D) != 0)
        return -1;
    return (long)FB_USER_VA;
}

static long sys_fbinfo(struct trapframe *tf)
{
    uint64_t uva = arg(tf, 0);
    struct qvos_fb_info info;

    if (!uva || !fb_active())
        return -1;
    info.width = fb_width();
    info.height = fb_height();
    info.bpp = 32;
    info.pitch = info.width * 4;
    info.size_bytes = info.pitch * info.height;
    info.phys = fb_paddr();

    if (copyout(myproc()->pg, &info, uva, sizeof(info)) != 0)
        return -1;
    return 0;
}

static long sys_fbflush(void)
{
    if (!fb_active())
        return -1;
    fb_flush();
    return 0;
}

static long sys_fbtext(struct trapframe *tf)
{
    uint64_t uva = arg(tf, 0);
    size_t n = (size_t)arg(tf, 1);
    char *buf;

    if (!fb_active())
        return -1;
    if (n == 0)
        return 0;
    if (n > 4096)
        n = 4096;
    buf = kmalloc(n);
    if (!buf)
        return -1;
    if (copyin(myproc()->pg, buf, uva, n) != 0) {
        kfree(buf);
        return -1;
    }
    fb_console_write(buf, n);
    kfree(buf);
    return (long)n;
}

static long sys_fbclear(struct trapframe *tf)
{
    if (!fb_active())
        return -1;
    fb_console_clear((uint32_t)arg(tf, 0));
    return 0;
}

void syscall(struct trapframe *tf)
{
    uint64_t no = tf->gpr[17];
    long ret = -1;

    switch (no) {
    case SYS_exit:    proc_exit((int)arg(tf, 0)); break;   /* no return */
    case SYS_fork:    ret = sys_fork(); break;
    case SYS_read:    ret = sys_read(tf); break;
    case SYS_write:   ret = sys_write(tf); break;
    case SYS_open:    ret = sys_open(tf); break;
    case SYS_close:   ret = sys_close(tf); break;
    case SYS_waitpid: ret = sys_waitpid(tf); break;
    case SYS_execve:
        /* proc_exec installs the new user context with a0 = argc; the
         * usual "a0 = ret" below would clobber argc with the exec return
         * value (0), which is why every shell-spawned program saw argc==0. */
        if (sys_execve(tf) == 0)
            return;
        ret = -1;
        break;
    case SYS_lseek:   ret = sys_lseek(tf); break;
    case SYS_getpid:  ret = sys_getpid(); break;
    case SYS_sbrk:    ret = sys_sbrk(tf); break;
    case SYS_getppid: ret = sys_getppid(); break;
    case SYS_yield:   ret = sys_yield(); break;
    case SYS_nanosleep: ret = sys_nanosleep(); break;
    case SYS_fstat:   ret = sys_fstat(tf); break;
    case SYS_dup:     ret = sys_dup(tf); break;
    case SYS_pipe:    ret = sys_pipe(tf); break;
    case SYS_getdents: ret = sys_getdents(tf); break;
    case SYS_reboot:  ret = sys_reboot(tf); break;
    case SYS_fbmap:   ret = sys_fbmap(); break;
    case SYS_fbinfo:  ret = sys_fbinfo(tf); break;
    case SYS_fbflush: ret = sys_fbflush(); break;
    case SYS_fbtext:  ret = sys_fbtext(tf); break;
    case SYS_fbclear: ret = sys_fbclear(tf); break;
    default:
        klog_puts("[qvos] unknown syscall ");
        klog_hex(no);
        klog_puts("\n");
        ret = -1;
    }

    tf->gpr[10] = (uint64_t)ret;
}
