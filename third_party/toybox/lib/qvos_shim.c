/* qvos compatibility shim for the toybox subset.  All functions are built
 * on top of the qvos syscalls already exported by user/libc.c. */
#include "toys.h"
#include <stdarg.h>

int errno;
char **environ;
static char *env_store[8];

/* ---- ctype ---- */
int tolower(int c){ return (c>='A'&&c<='Z') ? c+32 : c; }
int toupper(int c){ return (c>='a'&&c<='z') ? c-32 : c; }
int isalpha(int c){ return (c>='a'&&c<='z')||(c>='A'&&c<='Z'); }
int isdigit(int c){ return c>='0'&&c<='9'; }
int isalnum(int c){ return isalpha(c)||isdigit(c); }
int isspace(int c){ return c==' '||c=='\t'||c=='\n'||c=='\r'||c=='\v'||c=='\f'; }
int isprint(int c){ return c>=0x20&&c<0x7f; }
int isupper(int c){ return c>='A'&&c<='Z'; }
int islower(int c){ return c>='a'&&c<='z'; }
int isxdigit(int c){ return isdigit(c)||(c>='a'&&c<='f')||(c>='A'&&c<='F'); }
int ispunct(int c){ return isprint(c)&&!isalnum(c)&&!isspace(c); }

/* ---- byteswap ---- */
uint32_t bswap_32(uint32_t x){ return __builtin_bswap32(x); }
uint64_t bswap_64(uint64_t x){ return __builtin_bswap64(x); }

/* ---- string helpers ---- */
size_t strnlen(const char *s, size_t max)
{
    size_t n = 0;
    while (n < max && s[n]) n++;
    return n;
}

char *strchr(const char *s, int c)
{
    while (*s) {
        if (*s == (char)c) return (char *)s;
        s++;
    }
    return (char)c ? 0 : (char *)s;
}

char *strrchr(const char *s, int c)
{
    char *r = 0;
    do {
        if (*s == (char)c) r = (char *)s;
    } while (*s++);
    return r;
}

char *strstr(const char *h, const char *n)
{
    size_t l = strlen(n);
    if (!l) return (char *)h;
    for (; *h; h++)
        if (*h == *n && !memcmp(h, n, l)) return (char *)h;
    return 0;
}

char *strcasestr(const char *h, const char *n)
{
    size_t l = strlen(n);
    for (; *h; h++) {
        size_t i;
        for (i = 0; i < l; i++)
            if (tolower((unsigned char)h[i]) != tolower((unsigned char)n[i])) break;
        if (i == l) return (char *)h;
    }
    return 0;
}

int strcasecmp(const char *a, const char *b)
{
    while (*a && tolower((unsigned char)*a) == tolower((unsigned char)*b)) {
        a++; b++;
    }
    return tolower((unsigned char)*a) - tolower((unsigned char)*b);
}

int strncasecmp(const char *a, const char *b, size_t n)
{
    while (n-- && *a && tolower((unsigned char)*a) == tolower((unsigned char)*b)) {
        a++; b++;
    }
    return n == (size_t)-1 ? 0 : tolower((unsigned char)*a) - tolower((unsigned char)*b);
}

int memcmp(const void *a, const void *b, size_t n)
{
    const unsigned char *x = a, *y = b;
    for (size_t i = 0; i < n; i++)
        if (x[i] != y[i]) return x[i] - y[i];
    return 0;
}

void *memmove(void *dst, const void *src, size_t n)
{
    unsigned char *d = dst;
    const unsigned char *s = src;
    if (d <= s || d >= s + n) {
        while (n--) *d++ = *s++;
    } else {
        d += n; s += n;
        while (n--) *--d = *--s;
    }
    return dst;
}

void *memchr(const void *s, int c, size_t n)
{
    const unsigned char *p = s;
    while (n--)
        if (*p == (unsigned char)c) return (void *)p;
        else p++;
    return 0;
}

size_t strcspn(const char *s, const char *reject)
{
    size_t n = 0;
    while (s[n] && !strchr(reject, s[n])) n++;
    return n;
}

char *strpbrk(const char *s, const char *accept)
{
    while (*s)
        if (strchr(accept, *s)) return (char *)s;
        else s++;
    return 0;
}

char *strsep(char **sp, const char *delim)
{
    char *s = *sp;
    if (!s) return 0;
    char *p = strpbrk(s, delim);
    if (p) {
        *p = 0;
        *sp = p + 1;
    } else {
        *sp = 0;
    }
    return s;
}

char *strtok(char *s, const char *delim)
{
    static char *save;
    if (s) save = s;
    if (!save) return 0;
    while (*save && strchr(delim, *save)) save++;
    if (!*save) return 0;
    char *r = save;
    save = strpbrk(save, delim);
    if (save) *save++ = 0;
    return r;
}

char *strtok_r(char *s, const char *delim, char **save)
{
    if (s) *save = s;
    if (!*save) return 0;
    while (**save && strchr(delim, **save)) (*save)++;
    if (!**save) return 0;
    char *r = *save;
    *save = strpbrk(*save, delim);
    if (*save) *(*save)++ = 0;
    return r;
}

/* ---- strerror ---- */
char *strerror(int errnum)
{
    static const char *msg[] = {
        [ENOENT]="No such file or directory",
        [EBADF]="Bad file descriptor",
        [ENOMEM]="Out of memory",
        [EACCES]="Permission denied",
        [EEXIST]="File exists",
        [ENOTDIR]="Not a directory",
        [EISDIR]="Is a directory",
        [EINVAL]="Invalid argument",
        [ENOSYS]="Function not implemented",
        [EPIPE]="Broken pipe",
        [EROFS]="Read-only file system",
        [ENOTTY]="Inappropriate ioctl for device",
        [ENOEXEC]="Exec format error",
        [EIO]="I/O error",
    };
    static char unknown[24];
    if (errnum >= 0 && (size_t)errnum < sizeof(msg)/sizeof(msg[0]) && msg[errnum])
        return (char *)msg[errnum];
    snprintf(unknown, sizeof(unknown), "Error %d", errnum);
    return unknown;
}

/* ---- numeric parsing ---- */
long strtol(const char *s, char **end, int base)
{
    long long v = strtoll(s, end, base);
    return (long)v;
}

long long strtoll(const char *s, char **end, int base)
{
    int neg = 0;
    while (isspace((unsigned char)*s)) s++;
    if (*s == '-') { neg = 1; s++; }
    else if (*s == '+') s++;
    if ((base == 0 || base == 16) && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        base = 16; s += 2;
    }
    if (base == 0) base = *s == '0' ? 8 : 10;
    unsigned long long v = 0;
    while (*s) {
        int d;
        char c = *s;
        if (c >= '0' && c <= '9') d = c - '0';
        else if (c >= 'a' && c <= 'z') d = c - 'a' + 10;
        else if (c >= 'A' && c <= 'Z') d = c - 'A' + 10;
        else break;
        if (d >= base) break;
        v = v * (unsigned)base + (unsigned)d;
        s++;
    }
    if (end) *end = (char *)s;
    return neg ? -(long long)v : (long long)v;
}

unsigned long strtoul(const char *s, char **end, int base)
{
    return (unsigned long)strtoull(s, end, base);
}

unsigned long long strtoull(const char *s, char **end, int base)
{
    while (isspace((unsigned char)*s)) s++;
    if (*s == '+') s++;
    if ((base == 0 || base == 16) && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        base = 16; s += 2;
    }
    if (base == 0) base = *s == '0' ? 8 : 10;
    unsigned long long v = 0;
    while (*s) {
        int d;
        char c = *s;
        if (c >= '0' && c <= '9') d = c - '0';
        else if (c >= 'a' && c <= 'z') d = c - 'a' + 10;
        else if (c >= 'A' && c <= 'Z') d = c - 'A' + 10;
        else break;
        if (d >= base) break;
        v = v * (unsigned)base + (unsigned)d;
        s++;
    }
    if (end) *end = (char *)s;
    return v;
}

int atoi(const char *s){ return (int)strtol(s, 0, 10); }
long atol(const char *s){ return strtol(s, 0, 10); }
long long atoll(const char *s){ return strtoll(s, 0, 10); }

/* ---- memory / allocation ---- */
void *calloc(size_t n, size_t size)
{
    void *p = malloc(n * size);
    if (p) memset(p, 0, n * size);
    return p;
}

void *realloc(void *p, size_t n)
{
    if (!p) return malloc(n);
    union { long a; char b; } *h = 0;
    return p;   /* qvos malloc has no realloc: keep old block, shrink/waste */
}

void *reallocarray(void *p, size_t n, size_t size){ return realloc(p, n*size); }

char *strdup(const char *s)
{
    size_t n = strlen(s) + 1;
    char *r = malloc(n);
    return r ? memcpy(r, s, n) : 0;
}

char *strndup(const char *s, size_t n)
{
    size_t l = strnlen(s, n);
    char *r = malloc(l + 1);
    if (r) {
        memcpy(r, s, l);
        r[l] = 0;
    }
    return r;
}

/* ---- environment ---- */
char *getenv(const char *name)
{
    size_t l = strlen(name);
    for (int i = 0; i < 8 && env_store[i]; i++) {
        char *eq = strchr(env_store[i], '=');
        if (eq && (size_t)(eq - env_store[i]) == l && !memcmp(env_store[i], name, l))
            return eq + 1;
    }
    return 0;
}

int setenv(const char *name, const char *value, int overwrite)
{
    if (!overwrite && getenv(name)) return 0;
    unsetenv(name);
    for (int i = 0; i < 8; i++) {
        if (!env_store[i]) {
            size_t l = strlen(name) + strlen(value) + 2;
            env_store[i] = malloc(l);
            if (!env_store[i]) return -1;
            snprintf(env_store[i], l, "%s=%s", name, value);
            environ = env_store;
            return 0;
        }
    }
    return -1;
}

int unsetenv(const char *name)
{
    size_t l = strlen(name);
    for (int i = 0; i < 8 && env_store[i]; i++) {
        char *eq = strchr(env_store[i], '=');
        if (eq && (size_t)(eq - env_store[i]) == l && !memcmp(env_store[i], name, l)) {
            free(env_store[i]);
            env_store[i] = 0;
        }
    }
    return 0;
}

/* ---- qsort (simple shell sort, fine for tiny directories) ---- */
void qsort(void *base, size_t nmemb, size_t size,
           int (*cmp)(const void *, const void *))
{
    char *p = base;
    for (size_t gap = nmemb / 2; gap; gap /= 2)
        for (size_t i = gap; i < nmemb; i++)
            for (size_t j = i; j >= gap &&
                 cmp(p + (j - gap) * size, p + j * size) > 0; j -= gap) {
                char tmp[64];
                size_t n = size < sizeof(tmp) ? size : sizeof(tmp);
                memcpy(tmp, p + j * size, n);
                memmove(p + j * size, p + (j - gap) * size, n);
                memcpy(p + (j - gap) * size, tmp, n);
            }
}

/* ---- stdio extras ---- */
static FILE files_open[8];

FILE *fdopen(int fd, const char *mode)
{
    (void)mode;
    FILE *f = calloc(1, sizeof(FILE));
    if (f) f->fd = fd;
    return f;
}

FILE *fopen(const char *path, const char *mode)
{
    int flags = 0;
    if (strchr(mode, 'r')) flags = 0;
    else if (strchr(mode, 'a')) flags = 1;
    else if (strchr(mode, 'w')) flags = 1;
    int fd = open(path, flags);
    if (fd < 0) return 0;
    return fdopen(fd, mode);
}

int fclose(FILE *f)
{
    if (!f) return -1;
    if (f != stdin && f != stdout && f != stderr)
        close(f->fd);
    free(f);
    return 0;
}

ssize_t getdelim(char **lineptr, size_t *n, int delim, FILE *f)
{
    size_t len = 0;
    char *buf = *lineptr;
    size_t cap = n ? *n : 0;
    for (;;) {
        int c = fgetc(f);
        if (c < 0 && !len) return -1;
        if (len + 1 >= cap) {
            cap = cap ? cap * 2 : 64;
            buf = realloc(buf, cap);
            *lineptr = buf;
            if (n) *n = cap;
        }
        if (c < 0) break;
        buf[len++] = (char)c;
        if (c == delim) break;
    }
    if (buf) buf[len] = 0;
    return (ssize_t)len;
}

ssize_t getline(char **lineptr, size_t *n, FILE *f)
{
    return getdelim(lineptr, n, '\n', f);
}

void perror(const char *s)
{
    if (s && *s) fprintf(stderr, "%s: ", s);
    fprintf(stderr, "%s\n", strerror(errno));
}

/* ---- dirent over qvos getdents ---- */
DIR *opendir(const char *path)
{
    int fd = open(path, 0);
    if (fd < 0) return 0;
    DIR *d = calloc(1, sizeof(DIR));
    if (!d) { close(fd); return 0; }
    d->fd = fd;
    return d;
}

struct dirent *readdir(DIR *d)
{
    static struct dirent de;
    if (!d) return 0;
    struct qvos_dirent qde;
    int n = getdents(d->fd, &qde, 1);
    if (n <= 0) return 0;
    de.d_ino = qde.d_ino;
    de.d_type = qde.d_type;
    strcpy(de.d_name, qde.d_name);
    return &de;
}

int closedir(DIR *d)
{
    if (!d) return -1;
    close(d->fd);
    free(d);
    return 0;
}

void rewinddir(DIR *d){ if (d) lseek(d->fd, 0, SEEK_SET); }
int dirfd(DIR *d){ return d ? d->fd : -1; }
DIR *fdopendir(int fd)
{
    DIR *d = calloc(1, sizeof(DIR));
    if (d) d->fd = fd;
    return d;
}

/* ---- stat ---- */
#pragma push_macro("fstat")
#undef fstat
static int libc_fstat_syscall(int fd, struct qvos_stat *st)
{
    return fstat(fd, st);
}
#pragma pop_macro("fstat")

static int qvos_fstat_to_stat(int fd, struct stat *st)
{
    struct qvos_stat qst;
    if (libc_fstat_syscall(fd, &qst) != 0) return -1;
    memset(st, 0, sizeof(*st));
    st->st_size = qst.st_size;
    switch (qst.st_mode) {
    case 1: st->st_mode = S_IFDIR | 0755; break;
    case 3: st->st_mode = S_IFBLK | 0600; break;
    case 2: st->st_mode = S_IFCHR | 0600; break;
    default: st->st_mode = S_IFREG | 0644; break;
    }
    st->st_uid = st->st_gid = 100;
    st->st_nlink = 1;
    st->st_blksize = 4096;
    st->st_blocks = (qst.st_size + 511) / 512;
    return 0;
}

int qvos_fstat(int fd, struct stat *st)
{
    return qvos_fstat_to_stat(fd, st);
}

int stat(const char *path, struct stat *st)
{
    int fd = open(path, 0);
    if (fd < 0) return -1;
    int r = qvos_fstat_to_stat(fd, st);
    close(fd);
    return r;
}

int lstat(const char *path, struct stat *st){ return stat(path, st); }

int fstatat(int dirfd, const char *path, struct stat *st, int flags)
{
    (void)dirfd; (void)flags;
    return stat(path, st);
}

int statfs(const char *path, struct statvfs *st)
{
    (void)path;
    memset(st, 0, sizeof(*st));
    st->f_bsize = st->f_frsize = 4096;
    st->f_namemax = 255;
    return 0;
}

int fstatfs(int fd, struct statvfs *st)
{
    (void)fd;
    memset(st, 0, sizeof(*st));
    st->f_bsize = st->f_frsize = 4096;
    st->f_namemax = 255;
    return 0;
}

/* ---- path / fd helpers ---- */
char *getcwd(char *buf, size_t size)
{
    if (!buf) {
        buf = malloc(2);
        if (!buf) return 0;
        size = 2;
    }
    if (size < 2) return 0;
    buf[0] = '/'; buf[1] = 0;
    return buf;
}

int chdir(const char *path)
{
    (void)path;
    return 0;   /* qvos has a single root, chdir is a no-op */
}

int access(const char *path, int mode)
{
    (void)mode;
    int fd = open(path, 0);
    if (fd < 0) return -1;
    close(fd);
    return 0;
}

int openat(int dirfd, const char *path, int flags, ...)
{
    (void)dirfd;
    return open(path, flags);
}

int readlinkat(int dirfd, const char *path, char *buf, size_t size)
{
    (void)dirfd; (void)path; (void)buf; (void)size;
    errno = ENOENT;
    return -1;
}

ssize_t readlink(const char *path, char *buf, size_t size)
{
    return readlinkat(-1, path, buf, size);
}

int mkdirat(int dirfd, const char *path, mode_t mode)
{
    (void)dirfd; (void)path; (void)mode;
    errno = EROFS;
    return -1;
}

int fchmodat(int dirfd, const char *path, mode_t mode, int flags)
{
    (void)dirfd; (void)path; (void)mode; (void)flags;
    errno = EROFS;
    return -1;
}

int fchmod(int fd, mode_t mode){ (void)fd; (void)mode; errno = EROFS; return -1; }
int fchown(int fd, uid_t uid, gid_t gid){ (void)fd; (void)uid; (void)gid; errno = EROFS; return -1; }
int fchownat(int d, const char *p, uid_t u, gid_t g, int f){ (void)d;(void)p;(void)u;(void)g;(void)f; errno=EROFS; return -1; }
int rename(const char *o, const char *n){ (void)o;(void)n; errno=EROFS; return -1; }
int link(const char *a, const char *b){ (void)a;(void)b; errno=EROFS; return -1; }
int symlink(const char *a, const char *b){ (void)a;(void)b; errno=EROFS; return -1; }
int unlink(const char *p){ (void)p; errno=EROFS; return -1; }
int fsync(int fd){ (void)fd; return 0; }
int ftruncate(int fd, off_t len){ (void)fd;(void)len; errno=EROFS; return -1; }
int truncate(const char *p, off_t len){ (void)p;(void)len; errno=EROFS; return -1; }
int mkdir(const char *p, mode_t m){ (void)p;(void)m; errno=EROFS; return -1; }
int rmdir(const char *p){ (void)p; errno=EROFS; return -1; }

int dup2(int oldfd, int newfd)
{
    if (oldfd == newfd) return newfd;
    close(newfd);
    int fd = dup(oldfd);
    return fd == newfd ? fd : -1;
}

int isatty(int fd)
{
    struct qvos_stat st;
    return libc_fstat_syscall(fd, &st) == 0 && (st.st_mode & 0xffff) == 2;
}

/* ---- time ---- */
static long long boot_ms;
int gettimeofday(struct timeval *tv, struct timezone *tz)
{
    if (tv) { tv->tv_sec = boot_ms / 1000; tv->tv_usec = (boot_ms % 1000) * 1000; }
    if (tz) memset(tz, 0, sizeof(*tz));
    return 0;
}

long clock_gettime(int clk, struct timespec *ts)
{
    (void)clk;
    if (ts) { ts->tv_sec = boot_ms / 1000; ts->tv_nsec = (boot_ms % 1000) * 1000000L; }
    return 0;
}

unsigned sleep(unsigned seconds)
{
    unsigned i = seconds * 100;
    while (i--) qvos_nanosleep(0, 0);
    return 0;
}

int usleep(useconds_t usec)
{
    (void)usec;
    qvos_nanosleep(0, 0);
    return 0;
}

time_t time(time_t *t)
{
    time_t v = boot_ms / 1000;
    if (t) *t = v;
    return v;
}

clock_t clock(void){ return (clock_t)boot_ms; }
time_t mktime(struct tm *tm)
{
    (void)tm;
    return boot_ms / 1000;
}

struct tm *localtime(const time_t *t)
{
    static struct tm tm;
    memset(&tm, 0, sizeof(tm));
    time_t v = t ? *t : boot_ms / 1000;
    tm.tm_sec = v % 60; v /= 60;
    tm.tm_min = v % 60; v /= 60;
    tm.tm_hour = v % 24; v /= 24;
    tm.tm_mday = 1;
    tm.tm_year = 70;
    return &tm;
}

struct tm *gmtime(const time_t *t){ return localtime(t); }

/* ---- process ---- */
int execv(const char *path, char *const argv[])
{
    return execve(path, argv, environ);
}

int execvp(const char *file, char *const argv[])
{
    if (strchr(file, '/')) return execv(file, argv);
    char path[128];
    const char *dirs = getenv("PATH") ? getenv("PATH") : "/bin";
    const char *d = dirs;
    while (*d) {
        const char *e = strchr(d, ':');
        size_t l = e ? (size_t)(e - d) : strlen(d);
        if (l + strlen(file) + 2 < sizeof(path)) {
            memcpy(path, d, l);
            path[l] = '/';
            strcpy(path + l + 1, file);
            if (access(path, 0) == 0) return execv(path, argv);
        }
        if (!e) break;
        d = e + 1;
    }
    errno = ENOENT;
    return -1;
}

pid_t vfork(void){ return fork(); }
pid_t xfork(void)
{
    pid_t pid = fork();
    if (pid < 0) fprintf(stderr, "fork failed\n");
    return pid;
}

int kill(pid_t pid, int sig){ (void)pid; (void)sig; errno = ENOSYS; return -1; }
int getuid(void){ return 100; }
int geteuid(void){ return 100; }
int getgid(void){ return 100; }
int getegid(void){ return 100; }
int setpgid(pid_t pid, pid_t pgid){ (void)pid;(void)pgid; return 0; }
pid_t getpgrp(void){ return 1; }

int uname(struct utsname *u)
{
    memset(u, 0, sizeof(*u));
    strcpy(u->sysname, "qvos");
    strcpy(u->nodename, "qemu-virt");
    strcpy(u->release, "0.1");
    strcpy(u->version, "AI");
    strcpy(u->machine, "riscv64");
    return 0;
}

/* ---- signals / setjmp (small RISC-V assembly implementation) ---- */
int setjmp(jmp_buf env)
{
    __asm__ volatile(
        "sd ra, 0(a0)\n\t"
        "sd sp, 8(a0)\n\t"
        "sd s0, 16(a0)\n\t"
        "sd s1, 24(a0)\n\t"
        "sd s2, 32(a0)\n\t"
        "sd s3, 40(a0)\n\t"
        "sd s4, 48(a0)\n\t"
        "sd s5, 56(a0)\n\t"
        "sd s6, 64(a0)\n\t"
        "sd s7, 72(a0)\n\t"
        "sd s8, 80(a0)\n\t"
        "sd s9, 88(a0)\n\t"
        "sd s10, 96(a0)\n\t"
        "sd s11, 104(a0)\n\t"
        "li a0, 0\n\t"
        "ret\n\t");
    return 0;
}

void longjmp(jmp_buf env, int val)
{
    __asm__ volatile(
        "ld ra, 0(a0)\n\t"
        "ld sp, 8(a0)\n\t"
        "ld s0, 16(a0)\n\t"
        "ld s1, 24(a0)\n\t"
        "ld s2, 32(a0)\n\t"
        "ld s3, 40(a0)\n\t"
        "ld s4, 48(a0)\n\t"
        "ld s5, 56(a0)\n\t"
        "ld s6, 64(a0)\n\t"
        "ld s7, 72(a0)\n\t"
        "ld s8, 80(a0)\n\t"
        "ld s9, 88(a0)\n\t"
        "ld s10, 96(a0)\n\t"
        "ld s11, 104(a0)\n\t"
        "seqz a1, a1\n\t"
        "add a0, a0, a1\n\t"
        "ret\n\t");
    for (;;) {}
}

int sigsetjmp(sigjmp_buf env, int savesigs){ (void)savesigs; return setjmp(env); }
void siglongjmp(sigjmp_buf env, int val){ longjmp(env, val); }
sighandler_t signal(int sig, sighandler_t h){ (void)sig; return h; }
int sigaction(int sig, const void *act, void *old){ (void)sig;(void)act;(void)old; return 0; }
int sigemptyset(void *set){ if(set) memset(set,0,16); return 0; }
int sigfillset(void *set){ if(set) memset(set,0xff,16); return 0; }
int sigaddset(void *set, int sig){ (void)set;(void)sig; return 0; }
int sigprocmask(int how, const void *set, void *old){ (void)how;(void)set;(void)old; return 0; }

/* ---- mmap / misc ---- */
void *mmap(void *addr, size_t len, int prot, int flags, int fd, off_t off)
{
    (void)addr;(void)len;(void)prot;(void)flags;(void)fd;(void)off;
    errno = ENOSYS;
    return (void *)-1;
}

int munmap(void *addr, size_t len){ (void)addr;(void)len; return 0; }
int mprotect(void *addr, size_t len, int prot){ (void)addr;(void)len;(void)prot; return 0; }
int ioctl(int fd, unsigned long req, ...){ (void)fd;(void)req; errno=ENOTTY; return -1; }
int fcntl(int fd, int cmd, ...){ (void)fd;(void)cmd; return 0; }
int tcgetattr(int fd, struct termios *t){ (void)fd; if(t) memset(t,0,sizeof(*t)); return 0; }
int tcsetattr(int fd, int opt, const struct termios *t){ (void)fd;(void)opt;(void)t; return 0; }
int getpriority(int which, int who){ (void)which;(void)who; return 0; }
int setpriority(int which, int who, int prio){ (void)which;(void)who;(void)prio; return 0; }
int nice(int inc){ (void)inc; return 0; }

/* ---- minimal terminal size (no real tty) ---- */
/* lib/tty.c provides terminal_size(); qvos has no real ioctl backend. */

/* ---- sscanf: enough for toybox's %u/%d/%x/%s parsing ---- */
int sscanf(const char *s, const char *fmt, ...)
{
    va_list ap;
    int assigned = 0;
    va_start(ap, fmt);

    while (*fmt) {
        while (*fmt == ' ' || *fmt == '\t') fmt++;
        if (!*fmt) break;
        if (*fmt != '%') {
            if (*s != *fmt) break;
            s++; fmt++;
            continue;
        }
        fmt++;
        if (*fmt == '%') {
            if (*s++ != '%') break;
            fmt++;
            continue;
        }
        while (*s == ' ' || *s == '\t') s++;
        if (*fmt == 'u' || *fmt == 'd' || *fmt == 'x') {
            char *end = 0;
            long long v = strtoll(s, &end, *fmt == 'x' ? 16 : 10);
            if (end == s) break;
            if (*fmt == 'd') *va_arg(ap, int *) = (int)v;
            else if (*fmt == 'u') *va_arg(ap, unsigned *) = (unsigned)v;
            else *va_arg(ap, unsigned *) = (unsigned)v;
            assigned++;
            s = end;
        } else if (*fmt == 's') {
            char *d = va_arg(ap, char *);
            while (*s && *s != ' ' && *s != '\t') *d++ = *s++;
            *d = 0;
            assigned++;
        } else if (*fmt == 'c') {
            *va_arg(ap, char *) = *s++;
            assigned++;
        } else {
            break;
        }
        fmt++;
    }
    va_end(ap);
    return assigned;
}
void cfmakeraw(struct termios *t){ if (t) memset(t, 0, sizeof(*t)); }
int cfsetspeed(struct termios *t, int speed){ (void)t;(void)speed; return 0; }

/* ---- wide char / utf8 stubs ---- */
int wcwidth(int wc){ (void)wc; return 1; }
int towlower(int c){ return tolower(c); }
size_t wcrtomb(char *s, wchar_t wc, void *ps)
{
    (void)ps;
    if (s) {
        if (wc > 0x7f) return (size_t)-1;
        *s = (char)wc;
    }
    return 1;
}

/* ---- network stubs (not used by the qvos applet subset) ---- */
int socket(int d, int t, int p){ (void)d;(void)t;(void)p; errno=ENOSYS; return -1; }
int bind(int fd, const struct sockaddr *a, uint32_t l){ (void)fd;(void)a;(void)l; errno=ENOSYS; return -1; }
int connect(int fd, const struct sockaddr *a, uint32_t l){ (void)fd;(void)a;(void)l; errno=ENOSYS; return -1; }
int setsockopt(int fd, int l, int o, const void *v, uint32_t n){ (void)fd;(void)l;(void)o;(void)v;(void)n; return 0; }
int getaddrinfo(const char *n, const char *s, const struct addrinfo *h, struct addrinfo **r){ (void)n;(void)s;(void)h;(void)r; return -1; }
void freeaddrinfo(struct addrinfo *r){ (void)r; }
const char *gai_strerror(int err){ return strerror(err); }

/* ---- block size / math / hash helpers (unused subset fallbacks) ---- */
int get_block_device_size(int fd, unsigned long long *size){ (void)fd;(void)size; errno=ENOSYS; return -1; }
double sin(double x){ (void)x; return 0; }
double cos(double x){ (void)x; return 0; }
double tan(double x){ (void)x; return 0; }
double fabs(double x){ return x < 0 ? -x : x; }
double sqrt(double x){ (void)x; return 0; }
double pow(double x, double y){ (void)x;(void)y; return 0; }
double log(double x){ (void)x; return 0; }
double floor(double x){ return (double)(long long)x; }
double ceil(double x){ long long i=(long long)x; return i == x ? (double)i : (double)(i+1); }
double strtod(const char *s, char **end){ return (double)strtoll(s, end, 10); }

/* ---- extra stubs ---- */
int qvos_nanosleep(const void *req, const void *rem)
{
    (void)req; (void)rem;
    return (int)syscall6(SYS_nanosleep, 0, 0, 0, 0, 0, 0);
}

char *stpcpy(char *dst, const char *src)
{
    char *d = dst;
    while ((*d++ = *src++)) ;
    return d - 1;
}

int mkstemp(char *tmpl){ (void)tmpl; errno = EROFS; return -1; }
pid_t setsid(void){ return 1; }
int chroot(const char *path){ (void)path; return 0; }
int umask(mode_t mask){ (void)mask; return 022; }
int setuid(uid_t uid){ (void)uid; return 0; }
int setgid(gid_t gid){ (void)gid; return 0; }
int seteuid(uid_t uid){ (void)uid; return 0; }
int setegid(gid_t gid){ (void)gid; return 0; }

char *basename(char *path)
{
    if (!path) return (char *)".";
    char *p = path + strlen(path);
    while (p > path && p[-1] == '/') *--p = 0;
    char *base = strrchr(path, '/');
    return base ? base + 1 : path;
}

char *dirname(char *path)
{
    if (!path) return (char *)".";
    char *p = strrchr(path, '/');
    if (!p) return (char *)".";
    while (p > path && p[-1] == '/') p--;
    *p = 0;
    return path[0] ? path : (char *)"/";
}

char *setlocale(int cat, const char *loc){ (void)cat; return (char *)"C"; }
char *nl_langinfo(int item){ (void)item; return (char *)"ASCII"; }
void *newlocale(int mask, const char *loc, void *base){ (void)mask;(void)loc; return base; }
void *uselocale(void *loc){ return loc; }

static struct passwd root_pw = { "root", "", 100, 100, "qvos", "/", "/bin/sh" };
static struct group root_gr = { "root", "", 100, 0 };

struct passwd *getpwnam(const char *name)
{
    return !strcmp(name, "root") ? &root_pw : 0;
}

struct passwd *getpwuid(uid_t uid){ return uid == 100 ? &root_pw : 0; }
struct group *getgrnam(const char *name){ return !strcmp(name, "root") ? &root_gr : 0; }
struct group *getgrgid(gid_t gid){ return gid == 100 ? &root_gr : 0; }

int getpwnam_r(const char *n, struct passwd *p, char *b, size_t s, struct passwd **r)
{
    (void)b; (void)s;
    struct passwd *q = getpwnam(n);
    if (!q) return ENOENT;
    *p = *q;
    if (r) *r = p;
    return 0;
}

int getpwuid_r(uid_t u, struct passwd *p, char *b, size_t s, struct passwd **r)
{
    (void)b; (void)s;
    struct passwd *q = getpwuid(u);
    if (!q) return ENOENT;
    *p = *q;
    if (r) *r = p;
    return 0;
}

int getgrnam_r(const char *n, struct group *g, char *b, size_t s, struct group **r)
{
    (void)b; (void)s;
    struct group *q = getgrnam(n);
    if (!q) return ENOENT;
    *g = *q;
    if (r) *r = g;
    return 0;
}

int getgrgid_r(gid_t u, struct group *g, char *b, size_t s, struct group **r)
{
    (void)b; (void)s;
    struct group *q = getgrgid(u);
    if (!q) return ENOENT;
    *g = *q;
    if (r) *r = g;
    return 0;
}

int regexec(const regex_t *re, const char *s, size_t n, regmatch_t m[], int flags)
{
    (void)re; (void)s; (void)n; (void)m; (void)flags;
    errno = ENOSYS;
    return 1;
}

void xgetrandom(void *buf, unsigned len)
{
    unsigned char *p = buf;
    static unsigned seed = 0x12345678;
    for (unsigned i = 0; i < len; i++) {
        seed = seed * 1664525U + 1013904223U;
        p[i] = (unsigned char)(seed >> 24);
    }
}

void xsignal_all_killers(void *handler)
{
    (void)handler;
}

char *num_to_sig(int sig)
{
    static char buf[16];
    switch (sig) {
    case SIGHUP: return "HUP";
    case SIGINT: return "INT";
    case SIGKILL: return "KILL";
    case SIGTERM: return "TERM";
    default:
        snprintf(buf, sizeof(buf), "%d", sig);
        return buf;
    }
}

/* ---- extra extra stubs ---- */
int dev_major(dev_t dev){ return (int)(dev >> 16); }
int dev_minor(dev_t dev){ return (int)(dev & 0xffff); }
dev_t dev_makedev(int major, int minor){ return ((dev_t)major << 16) | (unsigned)minor; }

struct tm *localtime_r(const time_t *t, struct tm *tm)
{
    struct tm *src = localtime(t);
    if (src && tm) *tm = *src;
    return tm;
}

char *strptime(const char *s, const char *fmt, struct tm *tm)
{
    (void)fmt;
    if (tm) memset(tm, 0, sizeof(*tm));
    return (char *)s;
}

int initgroups(const char *user, gid_t group){ (void)user;(void)group; return 0; }
int regcomp(regex_t *re, const char *pat, int flags){ (void)re;(void)pat;(void)flags; return 1; }
size_t regerror(int err, const regex_t *re, char *buf, size_t size)
{
    (void)err;(void)re;
    if (buf && size) { strcpy(buf, "regex error"); return strlen(buf); }
    return 0;
}
void tzset(void){}
ssize_t writev(int fd, const struct iovec *iov, int iovcnt)
{
    ssize_t total = 0;
    for (int i = 0; i < iovcnt; i++) {
        ssize_t w = write(fd, iov[i].iov_base, iov[i].iov_len);
        if (w < 0) return -1;
        total += w;
    }
    return total;
}

void openlog(const char *ident, int option, int facility){ (void)ident;(void)option;(void)facility; }
void syslog(int priority, const char *fmt, ...)
{
    (void)priority;
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
}
void vsyslog(int priority, const char *fmt, va_list ap)
{
    (void)priority;
    vfprintf(stderr, fmt, ap);
}
void closelog(void){}

struct xnotify { int dummy; };
struct xnotify *xnotify_init(int max){ (void)max; return 0; }
int xnotify_add(struct xnotify *n, int fd, char *path){ (void)n;(void)fd;(void)path; return -1; }
int xnotify_wait(struct xnotify *n, char **path){ (void)n;(void)path; return -1; }

char *ctime(const time_t *t)
{
    static char buf[32];
    time_t v = t ? *t : 0;
    snprintf(buf, sizeof(buf), "Thu Jan  1 %02lld:%02lld:%02lld 1970",
             (long long)((v / 3600) % 24), (long long)((v / 60) % 60),
             (long long)(v % 60));
    return buf;
}

char *asctime(const struct tm *tm)
{
    static char buf[32];
    snprintf(buf, sizeof(buf), "Thu Jan  1 %02d:%02d:%02d 1970",
             tm ? tm->tm_hour : 0, tm ? tm->tm_min : 0, tm ? tm->tm_sec : 0);
    return buf;
}

size_t strftime(char *s, size_t max, const char *fmt, const struct tm *tm)
{
    (void)fmt;(void)tm;
    if (s && max) s[0] = 0;
    return 0;
}

/* ---- final link shims ---- */
int abs(int x){ return x < 0 ? -x : x; }
long labs(long x){ return x < 0 ? -x : x; }
long long llabs(long long x){ return x < 0 ? -x : x; }
int putchar(int c){ return fputc(c, stdout); }
int setvbuf(FILE *f, char *buf, int mode, size_t size){ (void)f;(void)buf;(void)mode;(void)size; return 0; }
void _exit(int status)
{
    syscall6(SYS_exit, status, 0, 0, 0, 0, 0);
    for (;;) {}
}

long long sendfile_len(int in, int out, long long bytes, long long *consumed)
{
    static char buf[4096];
    long long done = 0;
    while (bytes < 0 || done < bytes) {
        size_t want = sizeof(buf);
        if (bytes >= 0 && (long long)want > bytes - done) want = (size_t)(bytes - done);
        ssize_t r = read(in, buf, want);
        if (r <= 0) break;
        ssize_t w = write(out, buf, (size_t)r);
        if (w <= 0) break;
        done += w;
        if (w != r) break;
    }
    if (consumed) *consumed = done;
    return done;
}

int xpoll(struct pollfd *fds, int nfds, int timeout)
{
    (void)fds; (void)nfds; (void)timeout;
    qvos_nanosleep(0, 0);
    return nfds ? 0 : -1;
}
