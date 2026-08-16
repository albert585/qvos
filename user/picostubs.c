/* qvos syscall glue for picolibc.
 *
 * picolibc's libc.a expects a small set of POSIX-shaped OS functions
 * (read/write/open/...), plus _exit.  These translate directly onto the
 * qvos ecall ABI defined in include/kernel/syscall.h.  Strong definitions
 * here win over the weak fallbacks inside libc.a.
 */
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <pwd.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/times.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include <kernel/syscall.h>

static long
qsys(long n, long a0, long a1, long a2, long a3, long a4, long a5)
{
    register long ra0 __asm__("a0") = a0;
    register long ra1 __asm__("a1") = a1;
    register long ra2 __asm__("a2") = a2;
    register long ra3 __asm__("a3") = a3;
    register long ra4 __asm__("a4") = a4;
    register long ra5 __asm__("a5") = a5;
    register long ra7 __asm__("a7") = n;
    __asm__ volatile("ecall" : "+r"(ra0)
                     : "r"(ra1), "r"(ra2), "r"(ra3), "r"(ra4),
                       "r"(ra5), "r"(ra7)
                     : "memory");
    return ra0;
}

/* qvos has no *at() syscalls, but toybox's dirtree passes dirfd + bare
 * name.  Remember each fd's absolute path here so openat/fstatat can
 * rebuild a full path the qvos VFS understands. */
#define QVOS_FD_MAX 64
static char *fd_paths[QVOS_FD_MAX];

static void
fd_path_set(int fd, const char *path)
{
    if (fd < 0 || fd >= QVOS_FD_MAX)
        return;
    free(fd_paths[fd]);
    fd_paths[fd] = path ? strdup(path) : 0;
}

static const char *
fd_path_get(int fd)
{
    if (fd < 0 || fd >= QVOS_FD_MAX)
        return 0;
    return fd_paths[fd];
}

static void
fd_path_clear(int fd)
{
    fd_path_set(fd, 0);
}

/* "parent"/"name" -> one malloc'd absolute path ("/" never doubled). */
static char *
path_join(const char *parent, const char *name)
{
    size_t plen = parent ? strlen(parent) : 0;
    size_t nlen = name ? strlen(name) : 0;
    char *out;

    if (nlen && name[0] == '/')
        return strdup(name);
    if (!parent || !plen)
        parent = "/";
    out = malloc(plen + nlen + 2);
    if (!out)
        return 0;
    memcpy(out, parent, plen);
    if (plen && out[plen - 1] != '/')
        out[plen++] = '/';
    memcpy(out + plen, name, nlen);
    out[plen + nlen] = 0;
    return out;
}

void
_exit(int status)
{
    qsys(SYS_exit, status, 0, 0, 0, 0, 0);
    for (;;) {}
}

int
close(int fd)
{
    long r = qsys(SYS_close, fd, 0, 0, 0, 0, 0);
    if (r < 0) { errno = EBADF; return -1; }
    fd_path_clear(fd);
    return (int)r;
}

int
open(const char *path, int flags, ...)
{
    long r = qsys(SYS_open, (long)path, flags, 0, 0, 0, 0);
    if (r < 0) { errno = ENOENT; return -1; }
    fd_path_set((int)r, path);
    return (int)r;
}

ssize_t
read(int fd, void *buf, size_t n)
{
    long r = qsys(SYS_read, fd, (long)buf, (long)n, 0, 0, 0);
    if (r < 0) { errno = EIO; return -1; }
    return (ssize_t)r;
}

ssize_t
write(int fd, const void *buf, size_t n)
{
    long r = qsys(SYS_write, fd, (long)buf, (long)n, 0, 0, 0);
    if (r < 0) { errno = EIO; return -1; }
    return (ssize_t)r;
}

off_t
lseek(int fd, off_t off, int whence)
{
    long r = qsys(SYS_lseek, fd, (long)off, whence, 0, 0, 0);
    if (r < 0) { errno = EINVAL; return -1; }
    return (off_t)r;
}

/* ---- process / fd syscalls qvos already implements ------------------ */

pid_t
fork(void)
{
    long r = qsys(SYS_fork, 0, 0, 0, 0, 0, 0);
    if (r < 0) { errno = EAGAIN; return -1; }
    return (pid_t)r;
}

pid_t
waitpid(pid_t pid, int *status, int options)
{
    long r = qsys(SYS_waitpid, pid, (long)status, options, 0, 0, 0);
    if (r < 0) { errno = ECHILD; return -1; }
    return (pid_t)r;
}

int
execve(const char *path, char *const argv[], char *const envp[])
{
    long r = qsys(SYS_execve, (long)path, (long)argv, (long)envp, 0, 0, 0);
    if (r < 0) { errno = ENOENT; return -1; }
    return (int)r;
}

pid_t
getpid(void)
{
    return (pid_t)qsys(SYS_getpid, 0, 0, 0, 0, 0, 0);
}

pid_t
getppid(void)
{
    return (pid_t)qsys(SYS_getppid, 0, 0, 0, 0, 0, 0);
}

int
kill(pid_t pid, int sig)
{
    (void)pid; (void)sig;
    errno = ENOSYS;
    return -1;
}

int
dup(int oldfd)
{
    long r = qsys(SYS_dup, oldfd, 0, 0, 0, 0, 0);
    if (r < 0) { errno = EBADF; return -1; }
    fd_path_set((int)r, fd_path_get(oldfd));
    return (int)r;
}

int
dup2(int oldfd, int newfd)
{
    int got;

    if (oldfd == newfd)
        return newfd;
    close(newfd);
    for (;;) {
        got = dup(oldfd);
        if (got < 0)
            return -1;
        if (got == newfd)
            return newfd;
        if (got > newfd) {
            close(got);
            return newfd;
        }
    }
}

int
pipe(int fds[2])
{
    long r = qsys(SYS_pipe, (long)fds, 0, 0, 0, 0, 0);
    if (r < 0) { errno = EMFILE; return -1; }
    return 0;
}

int
nanosleep(const struct timespec *req, struct timespec *rem)
{
    (void)req; (void)rem;   /* qvos only has a coarse one-tick sleep */
    return (int)qsys(SYS_nanosleep, 0, 0, 0, 0, 0, 0);
}

struct iovec {
    void  *iov_base;
    size_t iov_len;
};

ssize_t
writev(int fd, const struct iovec *iov, int iovcnt)
{
    ssize_t total = 0;

    for (int i = 0; i < iovcnt; i++) {
        ssize_t w = write(fd, iov[i].iov_base, iov[i].iov_len);
        if (w < 0)
            return total ? total : -1;
        total += w;
    }
    return total;
}

/* ---- identity / clocks (qvos has no users, no RTC) ------------------ */

uid_t getuid(void)  { return 0; }
uid_t geteuid(void) { return 0; }
gid_t getgid(void)  { return 0; }
gid_t getegid(void) { return 0; }

int
gettimeofday(struct timeval *tv, void *tz)
{
    if (tv) {
        tv->tv_sec = 0;
        tv->tv_usec = 0;
    }
    (void)tz;
    return 0;
}

int
clock_gettime(clockid_t clk, struct timespec *ts)
{
    (void)clk;
    if (ts) {
        ts->tv_sec = 0;
        ts->tv_nsec = 0;
    }
    return 0;
}

clock_t
times(struct tms *buf)
{
    if (buf)
        memset(buf, 0, sizeof(*buf));
    return 0;
}

/* ---- stdio FILE objects (picolibc leaves these to the OS layer) ---- */

static int
q_put(char c, FILE *f)
{
    (void)f;
    return write(1, &c, 1) == 1 ? (unsigned char)c : EOF;
}

static int
q_put_err(char c, FILE *f)
{
    (void)f;
    return write(2, &c, 1) == 1 ? (unsigned char)c : EOF;
}

static int
q_get(FILE *f)
{
    unsigned char c;

    (void)f;
    ssize_t r = read(0, &c, 1);
    if (r == 1)
        return c;
    if (r < 0)
        return _FDEV_ERR;
    return _FDEV_EOF;
}

static FILE __stdin_f  = FDEV_SETUP_STREAM(NULL,      q_get, NULL, _FDEV_SETUP_READ);
static FILE __stdout_f = FDEV_SETUP_STREAM(q_put,     NULL,  NULL, _FDEV_SETUP_WRITE);
static FILE __stderr_f = FDEV_SETUP_STREAM(q_put_err, NULL,  NULL, _FDEV_SETUP_WRITE);

FILE *const stdin  = &__stdin_f;
FILE *const stdout = &__stdout_f;
FILE *const stderr = &__stderr_f;

int
isatty(int fd)
{
    /* qvos fd 0/1/2 are the kernel console; stdio uses this for buffering. */
    return (fd >= 0 && fd <= 2) ? 1 : 0;
}

/* qvos heap starts at the end of the loaded ELF (p->sz); SYS_sbrk grows it. */
void *
sbrk(ptrdiff_t inc)
{
    long r = qsys(SYS_sbrk, (long)inc, 0, 0, 0, 0, 0);
    if (r < 0) { errno = ENOMEM; return (void *)-1; }
    return (void *)r;
}

/* ---- stat ---------------------------------------------------------- */

struct qvos_stat {
    uint64_t st_size;
    uint32_t st_mode;
};

static void
fill_stat(struct stat *st, uint64_t size, uint32_t type)
{
    mode_t mode;

    memset(st, 0, sizeof(*st));
    /* qvos has no permissions/owners: only the POSIX type bits are real.
     * Keep the permission bits zero so ls -l shows "----------". */
    switch (type) {
    case 0:  mode = S_IFREG; break;
    case 1:  mode = S_IFDIR; break;
    case 2:  mode = S_IFCHR; break;
    case 3:  mode = S_IFBLK; break;
    default: mode = S_IFREG; break;
    }
    st->st_mode = mode;
    st->st_nlink = 1;
    st->st_size = (off_t)size;
    st->st_dev = 1;          /* toybox: all-zero dev/ino/blksize means "?" */
    st->st_ino = 1;
    st->st_blksize = 512;
    st->st_blocks = (size + 511) / 512;
}

int
fstat(int fd, struct stat *st)
{
    struct qvos_stat qs;
    long r = qsys(SYS_fstat, fd, (long)&qs, 0, 0, 0, 0);

    if (r < 0) { errno = EBADF; return -1; }
    fill_stat(st, qs.st_size, qs.st_mode);
    return 0;
}

int
stat(const char *path, struct stat *st)
{
    int fd = open(path, 0);
    if (fd < 0)
        return -1;
    int r = fstat(fd, st);
    close(fd);
    return r;
}

int
lstat(const char *path, struct stat *st)
{
    return stat(path, st);   /* qvos has no symlinks */
}

int
fstatat(int dirfd, const char *path, struct stat *st, int flags)
{
    (void)flags;
    if (dirfd == AT_FDCWD || !path || path[0] == '/')
        return stat(path, st);

    char *full = path_join(fd_path_get(dirfd), path);
    if (!full) { errno = ENOMEM; return -1; }
    int r = stat(full, st);
    free(full);
    return r;
}

/* ---- path/fd helpers (qvos has one root, no symlinks, no cwd) ------- */

int
openat(int dirfd, const char *path, int flags, ...)
{
    if (dirfd == AT_FDCWD || !path || path[0] == '/')
        return open(path, flags);

    char *full = path_join(fd_path_get(dirfd), path);
    if (!full) { errno = ENOMEM; return -1; }
    int r = open(full, flags);
    free(full);
    return r;
}

ssize_t
readlinkat(int dirfd, const char *path, char *buf, size_t size)
{
    (void)dirfd; (void)path; (void)buf; (void)size;
    errno = ENOENT;
    return -1;
}

ssize_t
readlink(const char *path, char *buf, size_t size)
{
    return readlinkat(-1, path, buf, size);
}

char *
getcwd(char *buf, size_t size)
{
    if (!buf) {
        buf = malloc(2);
        if (!buf) { errno = ENOMEM; return 0; }
        size = 2;
    }
    if (size < 2) { errno = ERANGE; return 0; }
    buf[0] = '/';
    buf[1] = 0;
    return buf;
}

int
chdir(const char *path)
{
    (void)path;
    return 0;   /* qvos paths are absolute; chdir is a no-op */
}

int
access(const char *path, int mode)
{
    (void)mode;
    int fd = open(path, 0);
    if (fd < 0)
        return -1;
    close(fd);
    return 0;
}

mode_t
umask(mode_t mask)
{
    (void)mask;
    return 022;
}

/* ---- passwd/group --------------------------------------------------
 * qvos has no /etc/passwd or /etc/group.  picolibc's _r variants return
 * without setting *result when fopen fails, which leaves toybox's local
 * "temp" uninitialized; always store NULL so callers fall back to the
 * numeric uid/gid string.
 */
int getpwuid_r(uid_t uid, struct passwd *pw, char *buf, size_t size,
               struct passwd **result)
{
    (void)uid; (void)pw; (void)buf; (void)size;
    if (result) *result = 0;
    return 0;
}

int getpwnam_r(const char *name, struct passwd *pw, char *buf, size_t size,
               struct passwd **result)
{
    (void)name; (void)pw; (void)buf; (void)size;
    if (result) *result = 0;
    return 0;
}

int getgrgid_r(gid_t gid, struct group *gr, char *buf, size_t size,
               struct group **result)
{
    (void)gid; (void)gr; (void)buf; (void)size;
    if (result) *result = 0;
    return 0;
}

int getgrnam_r(const char *name, struct group *gr, char *buf, size_t size,
               struct group **result)
{
    (void)name; (void)gr; (void)buf; (void)size;
    if (result) *result = 0;
    return 0;
}

int
fcntl(int fd, int cmd, ...)
{
    if (cmd == F_DUPFD) {
        va_list ap;
        va_start(ap, cmd);
        int min = va_arg(ap, int);
        va_end(ap);
        int got, base = min > fd ? min : fd + 1;
        for (;;) {
            got = dup(fd);
            if (got < 0)
                return -1;
            if (got >= base)
                return got;
        }
    }
    return 0;
}

int
ioctl(int fd, unsigned long req, ...)
{
    (void)fd; (void)req;
    errno = ENOTTY;
    return -1;
}

/* ---- dirent layer over SYS_getdents -------------------------------- */

static uint8_t
q_dt_to_dt(uint32_t t)
{
    switch (t) {
    case 0:  return DT_REG;
    case 1:  return DT_DIR;
    case 2:  return DT_CHR;
    case 3:  return DT_BLK;
    default: return DT_UNKNOWN;
    }
}

static int
dir_refill(DIR *d)
{
    int n = (int)qsys(SYS_getdents, d->fd, (long)d->buf,
                      (long)(sizeof(d->buf) / sizeof(struct qvos_dirent)),
                      0, 0, 0);
    if (n < 0)
        return -1;
    d->count = (size_t)n;
    d->offset = 0;
    return n;
}

DIR *
opendir(const char *name)
{
    DIR *d = malloc(sizeof(*d));
    if (!d) { errno = ENOMEM; return 0; }
    memset(d, 0, sizeof(*d));
    d->fd = open(name, 0);
    if (d->fd < 0) {
        free(d);
        return 0;
    }
    return d;
}

DIR *
fdopendir(int fd)
{
    if (fd < 0) { errno = EBADF; return 0; }
    DIR *d = malloc(sizeof(*d));
    if (!d) { errno = ENOMEM; return 0; }
    memset(d, 0, sizeof(*d));
    d->fd = fd;
    return d;
}

struct dirent *
readdir(DIR *d)
{
    struct qvos_dirent *q;

    if (!d) { errno = EBADF; return 0; }
    if (d->offset >= d->count) {
        int n = dir_refill(d);
        if (n <= 0)
            return 0;
    }
    q = &((struct qvos_dirent *)d->buf)[d->offset++];
    d->dirent.d_ino = q->d_ino;
    d->dirent.d_type = q_dt_to_dt(q->d_type);
    strncpy(d->dirent.d_name, q->d_name, sizeof(d->dirent.d_name) - 1);
    d->dirent.d_name[sizeof(d->dirent.d_name) - 1] = 0;
    return &d->dirent;
}

int
closedir(DIR *d)
{
    if (!d) { errno = EBADF; return -1; }
    int r = close(d->fd);
    free(d);
    return r;
}

/* ---- uname (picolibc has no <sys/utsname.h>) ----------------------- */

struct qvos_utsname {
    char sysname[65], nodename[65], release[65];
    char version[65], machine[65], domainname[65];
};

int
uname(struct qvos_utsname *u)
{
    if (!u) { errno = EFAULT; return -1; }
    memset(u, 0, sizeof(*u));
    strcpy(u->sysname, "qvos");
    strcpy(u->nodename, "qemu-virt");
    strcpy(u->release, "0.1");
    strcpy(u->version, "picolibc");
    strcpy(u->machine, "riscv64");
    return 0;
}

/* ---- not (yet) provided by qvos; fail cleanly with ENOSYS ---------- */

int unlink(const char *path) { (void)path; errno = ENOSYS; return -1; }
int link(const char *a, const char *b) { (void)a; (void)b; errno = ENOSYS; return -1; }
int rename(const char *a, const char *b) { (void)a; (void)b; errno = ENOSYS; return -1; }
