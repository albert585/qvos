/* qvos userspace libc subset: syscall wrappers, tiny stdio, string and a
 * first-fit malloc over sbrk.  Freestanding; no compiler runtime assumed. */
#include "libc.h"
#include <stdarg.h>

long syscall6(long n, long a0, long a1, long a2,
              long a3, long a4, long a5)
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

pid_t fork(void)        { return (pid_t)syscall6(SYS_fork,0,0,0,0,0,0); }
int waitpid(pid_t p, int *st, int o) { return (int)syscall6(SYS_waitpid,p,(long)st,o,0,0,0); }
int execve(const char *path, char *const argv[], char *const envp[])
{ return (int)syscall6(SYS_execve,(long)path,(long)argv,(long)envp,0,0,0); }
void exit(int status)   { syscall6(SYS_exit,status,0,0,0,0,0); for(;;){} }
pid_t getpid(void)      { return (pid_t)syscall6(SYS_getpid,0,0,0,0,0,0); }
pid_t getppid(void)     { return (pid_t)syscall6(SYS_getppid,0,0,0,0,0,0); }
int yield(void)         { return (int)syscall6(SYS_yield,0,0,0,0,0,0); }
int nanosleep(void)     { return (int)syscall6(SYS_nanosleep,0,0,0,0,0,0); }
int open(const char *p, int f, ...) { return (int)syscall6(SYS_open,(long)p,f,0,0,0,0); }
int close(int fd)       { return (int)syscall6(SYS_close,fd,0,0,0,0,0); }
ssize_t read(int fd, void *b, size_t n) { return (ssize_t)syscall6(SYS_read,fd,(long)b,n,0,0,0); }
ssize_t write(int fd, const void *b, size_t n) { return (ssize_t)syscall6(SYS_write,fd,(long)b,n,0,0,0); }
int lseek(int fd, int off, int whence) { return (int)syscall6(SYS_lseek,fd,off,whence,0,0,0); }
int dup(int fd)         { return (int)syscall6(SYS_dup,fd,0,0,0,0,0); }
int pipe(int fds[2])    { return (int)syscall6(SYS_pipe,(long)fds,0,0,0,0,0); }
int getdents(int fd, struct qvos_dirent *ents, int max)
{ return (int)syscall6(SYS_getdents, fd, (long)ents, max, 0, 0, 0); }
int reboot(int how)     { return (int)syscall6(SYS_reboot,how,0,0,0,0,0); }
int poweroff(void)      { return reboot(0x4321fedc); }
int fstat(int fd, struct qvos_stat *st) { return (int)syscall6(SYS_fstat,fd,(long)st,0,0,0,0); }
void *sbrk(intptr_t inc){ return (void *)syscall6(SYS_sbrk,inc,0,0,0,0,0); }

/* ---- virtio-gpu framebuffer ---- */
void *fbmap(void)
{
    long r = syscall6(SYS_fbmap, 0, 0, 0, 0, 0, 0);
    return r < 0 ? 0 : (void *)r;
}
int fbinfo(struct qvos_fb_info *info) { return (int)syscall6(SYS_fbinfo,(long)info,0,0,0,0,0); }
int fbflush(void)   { return (int)syscall6(SYS_fbflush,0,0,0,0,0,0); }
int fbtext(const char *s, size_t n) { return (int)syscall6(SYS_fbtext,(long)s,n,0,0,0,0); }
int fbclear(uint32_t bg) { return (int)syscall6(SYS_fbclear,bg,0,0,0,0,0); }

/* ---- tiny stdio ---- */
static FILE files_stdio[3] = {
    { .fd = 0 }, { .fd = 1 }, { .fd = 2 },
};

FILE *stdin = &files_stdio[0];
FILE *stdout = &files_stdio[1];
FILE *stderr = &files_stdio[2];

struct fmt_ctx {
    int is_file;
    FILE *f;
    char *buf;
    size_t cap;
    size_t len;
};

static int fmt_emit(void *vc, const char *p, size_t n)
{
    struct fmt_ctx *c = (struct fmt_ctx *)vc;

    if (c->is_file) {
        size_t off = 0;
        while (off < n) {
            ssize_t w = write(c->f->fd, p + off, n - off);
            if (w <= 0) {
                c->f->err = 1;
                break;
            }
            off += (size_t)w;
        }
        c->len += n;
        return 0;
    }
    c->len += n;
    if (c->buf && c->cap) {
        size_t room = c->cap - 1;
        size_t keep = (c->len - n) < room ? (c->len - n) : room;
        size_t take = (n < room - keep) ? n : room - keep;
        for (size_t i = 0; i < take; i++)
            c->buf[keep + i] = p[i];
        c->buf[keep + take] = 0;
    }
    return 0;
}

static void fmt_pad(struct fmt_ctx *c, int width, int zero, int left)
{
    char ch = zero ? '0' : ' ';
    while (width-- > 0)
        fmt_emit(c, &ch, 1);
    (void)left;
}

static void fmt_num(struct fmt_ctx *c, unsigned long long uv, int neg,
                    int base, int upper, int width, int prec, int zero, int left)
{
    static const char digs_l[] = "0123456789abcdef";
    static const char digs_u[] = "0123456789ABCDEF";
    const char *digs = upper ? digs_u : digs_l;
    char tmp[32];
    int n = 0;

    if (uv == 0 && prec != 0)
        tmp[n++] = '0';
    while (uv) {
        tmp[n++] = digs[uv % (unsigned)base];
        uv /= (unsigned)base;
    }
    int sign = neg ? 1 : 0;
    while (prec > n)
        tmp[n++] = '0';
    int total = sign + n;
    if (width > total && !left && !zero)
        fmt_pad(c, width - total, 0, 0);
    if (neg)
        fmt_emit(c, "-", 1);
    if (width > total && !left && zero)
        fmt_pad(c, width - total, 1, 0);
    while (n)
        fmt_emit(c, &tmp[--n], 1);
    if (width > total && left)
        fmt_pad(c, width - total, 0, 0);
}

static void fmt_core(struct fmt_ctx *c, const char *fmt, va_list ap)
{
    const char *p = fmt;

    while (*p) {
        if (*p != '%') {
            fmt_emit(c, p, 1);
            p++;
            continue;
        }
        p++;
        if (*p == '%') {
            fmt_emit(c, "%", 1);
            p++;
            continue;
        }
        int left = 0, zero = 0, width = 0, prec = -1, len_mod = 0;
        for (;; p++) {
            if (*p == '-') left = 1;
            else if (*p == '0') zero = 1;
            else break;
        }
        if (*p == '*') { width = va_arg(ap, int); p++; }
        else while (*p >= '0' && *p <= '9') width = width * 10 + (*p++ - '0');
        if (*p == '.') {
            p++;
            if (*p == '*') { prec = va_arg(ap, int); p++; }
            else {
                prec = 0;
                while (*p >= '0' && *p <= '9') prec = prec * 10 + (*p++ - '0');
            }
        }
        if (*p == 'l') { len_mod = 1; p++; }
        else if (*p == 'z') { len_mod = 1; p++; }
        if (*p == 'l') { len_mod = 2; p++; }
        char ch = *p++;
        if (ch == 'd' || ch == 'i') {
            long long sv;
            if (len_mod == 2) sv = va_arg(ap, long long);
            else if (len_mod == 1) sv = va_arg(ap, long);
            else sv = va_arg(ap, int);
            int neg = sv < 0;
            unsigned long long uv = neg ? (unsigned long long)(-(sv + 1)) + 1ULL
                                       : (unsigned long long)sv;
            fmt_num(c, uv, neg, 10, 0, width, prec, zero, left);
        } else if (ch == 'u') {
            unsigned long long uv = len_mod ? va_arg(ap, unsigned long long)
                                            : va_arg(ap, unsigned int);
            fmt_num(c, uv, 0, 10, 0, width, prec, zero, left);
        } else if (ch == 'x' || ch == 'X') {
            unsigned long long uv = len_mod ? va_arg(ap, unsigned long long)
                                            : va_arg(ap, unsigned int);
            fmt_num(c, uv, 0, 16, ch == 'X', width, prec, zero, left);
        } else if (ch == 'o') {
            unsigned long long uv = len_mod ? va_arg(ap, unsigned long long)
                                            : va_arg(ap, unsigned int);
            fmt_num(c, uv, 0, 8, 0, width, prec, zero, left);
        } else if (ch == 'p') {
            fmt_emit(c, "0x", 2);
            fmt_num(c, (unsigned long long)(uintptr_t)va_arg(ap, void *), 0,
                    16, 0, 0, -1, 1, 0);
        } else if (ch == 's') {
            const char *s = va_arg(ap, const char *);
            if (!s) s = "(null)";
            size_t n = strlen(s);
            if (prec >= 0 && (size_t)prec < n) n = (size_t)prec;
            int total = (int)n;
            if (width > total && !left)
                fmt_pad(c, width - total, 0, 0);
            fmt_emit(c, s, n);
            if (width > total && left)
                fmt_pad(c, width - total, 0, 0);
        } else if (ch == 'c') {
            char cc = (char)va_arg(ap, int);
            if (width > 1 && !left)
                fmt_pad(c, width - 1, 0, 0);
            fmt_emit(c, &cc, 1);
            if (width > 1 && left)
                fmt_pad(c, width - 1, 0, 0);
        } else {
            fmt_emit(c, &ch, 1);
        }
    }
}

int vfprintf(FILE *f, const char *fmt, va_list ap)
{
    struct fmt_ctx c = { .is_file = 1, .f = f ? f : stdout };
    fmt_core(&c, fmt, ap);
    return (int)c.len;
}

int vprintf(const char *fmt, va_list ap)
{
    return vfprintf(stdout, fmt, ap);
}

int vsnprintf(char *str, size_t size, const char *fmt, va_list ap)
{
    struct fmt_ctx c = { .is_file = 0, .buf = str, .cap = size };
    fmt_core(&c, fmt, ap);
    if (str && size)
        str[c.len < size ? c.len : size - 1] = 0;
    return (int)c.len;
}

int printf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int r = vfprintf(stdout, fmt, ap);
    va_end(ap);
    return r;
}

int fprintf(FILE *f, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int r = vfprintf(f, fmt, ap);
    va_end(ap);
    return r;
}

int snprintf(char *str, size_t size, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int r = vsnprintf(str, size, fmt, ap);
    va_end(ap);
    return r;
}

int sprintf(char *str, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int r = vsnprintf(str, ~(size_t)0, fmt, ap);
    va_end(ap);
    return r;
}

int fputc(int c, FILE *f)
{
    unsigned char ch = (unsigned char)c;
    if (write(f ? f->fd : 1, &ch, 1) != 1)
        f->err = 1;
    return c;
}

int putc(int c, FILE *f) { return fputc(c, f); }

int fputs(const char *s, FILE *f)
{
    size_t n = strlen(s);
    fwrite(s, 1, n, f);
    return n;
}

int puts(const char *s)
{
    fputs(s, stdout);
    fputc('\n', stdout);
    return 0;
}

size_t fwrite(const void *p, size_t size, size_t nmemb, FILE *f)
{
    size_t n = size * nmemb;
    const char *s = (const char *)p;
    size_t off = 0;
    while (off < n) {
        ssize_t w = write(f ? f->fd : 1, s + off, n - off);
        if (w <= 0) {
            f->err = 1;
            break;
        }
        off += (size_t)w;
    }
    return off / size;
}

size_t fread(void *p, size_t size, size_t nmemb, FILE *f)
{
    size_t want = size * nmemb;
    ssize_t r = read(f ? f->fd : 0, p, want);
    if (r <= 0) {
        f->eof = 1;
        return 0;
    }
    return (size_t)r / size;
}

int fgetc(FILE *f)
{
    unsigned char ch;
    if (f->ungot) {
        ch = (unsigned char)f->ungot;
        f->ungot = 0;
        return ch;
    }
    ssize_t r = read(f ? f->fd : 0, &ch, 1);
    if (r != 1) {
        f->eof = 1;
        return -1;
    }
    return ch;
}

int getc(FILE *f) { return fgetc(f); }

char *fgets(char *s, int size, FILE *f)
{
    if (size <= 0)
        return 0;
    int i = 0;
    while (i < size - 1) {
        int c = fgetc(f);
        if (c < 0)
            break;
        s[i++] = (char)c;
        if (c == '\n')
            break;
    }
    s[i] = 0;
    return i ? s : 0;
}

int fflush(FILE *f)
{
    (void)f;
    return 0;
}

int ferror(FILE *f) { return f ? f->err : 0; }
int feof(FILE *f)   { return f ? f->eof : 0; }
void clearerr(FILE *f)
{
    if (f) {
        f->err = 0;
        f->eof = 0;
    }
}

/* ---- string ---- */
size_t strlen(const char *s){ size_t n=0; while(s[n]) n++; return n; }
int strcmp(const char *a,const char *b){ while(*a && *a==*b){a++;b++;} return (unsigned char)*a-(unsigned char)*b; }
int strncmp(const char *a,const char *b,size_t n){ for(size_t i=0;i<n;i++){ if(a[i]!=b[i]||!a[i]) return (unsigned char)a[i]-(unsigned char)b[i]; } return 0; }
char *strcpy(char *d,const char *s){ char *r=d; while((*d++=*s++)) {} return r; }
char *strcat(char *d,const char *s){ char *r=d; while(*d) d++; while((*d++=*s++)) {} return r; }
void *memcpy(void *d,const void *s,size_t n){ unsigned char *x=(unsigned char*)d; const unsigned char *y=(const unsigned char*)s; for(size_t i=0;i<n;i++) x[i]=y[i]; return d; }
void *memset(void *s,int c,size_t n){ unsigned char *p=(unsigned char*)s; for(size_t i=0;i<n;i++) p[i]=(unsigned char)c; return s; }

/* ---- malloc over sbrk ---- */
union mhdr {
    struct { size_t size; int free; union mhdr *next; } s;
    long align;
};
static union mhdr *freelist;

void *malloc(size_t n)
{
    if (n == 0) n = 1;
    n = (n + sizeof(union mhdr) - 1) & ~(sizeof(union mhdr) - 1);

    union mhdr *prev = 0;
    for (union mhdr *b = freelist; b; prev = b, b = b->s.next) {
        if (b->s.free && b->s.size >= n) {
            if (b->s.size >= n + sizeof(union mhdr) + 16) {
                union mhdr *r = (union mhdr *)((char *)(b+1) + n);
                r->s.size = b->s.size - n - sizeof(union mhdr);
                r->s.free = 1;
                r->s.next = b->s.next;
                b->s.size = n;
                b->s.next = r;
            }
            b->s.free = 0;
            return (void *)(b + 1);
        }
    }

    union mhdr *b = (union mhdr *)sbrk((intptr_t)(n + sizeof(union mhdr)));
    if (b == (void *)-1) return 0;
    b->s.size = n;
    b->s.free = 0;
    b->s.next = 0;
    return (void *)(b + 1);
}

void free(void *p)
{
    if (!p) return;
    union mhdr *b = (union mhdr *)p - 1;
    b->s.free = 1;

    union mhdr *prev = 0;
    for (union mhdr *x = freelist; x; prev = x, x = x->s.next) {
        if ((char *)x + x->s.size + sizeof(union mhdr) == (char *)b) {
            x->s.size += sizeof(union mhdr) + b->s.size;
            b = x;
            b->s.free = 1;
        }
    }
    if (!prev) {
        b->s.next = freelist;
        freelist = b;
    }
}
