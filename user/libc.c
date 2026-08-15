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
int open(const char *p, int f) { return (int)syscall6(SYS_open,(long)p,f,0,0,0,0); }
int close(int fd)       { return (int)syscall6(SYS_close,fd,0,0,0,0,0); }
ssize_t read(int fd, void *b, size_t n) { return (ssize_t)syscall6(SYS_read,fd,(long)b,n,0,0,0); }
ssize_t write(int fd, const void *b, size_t n) { return (ssize_t)syscall6(SYS_write,fd,(long)b,n,0,0,0); }
int lseek(int fd, int off, int whence) { return (int)syscall6(SYS_lseek,fd,off,whence,0,0,0); }
int dup(int fd)         { return (int)syscall6(SYS_dup,fd,0,0,0,0,0); }
int pipe(int fds[2])    { return (int)syscall6(SYS_pipe,(long)fds,0,0,0,0,0); }
int reboot(int how)     { return (int)syscall6(SYS_reboot,how,0,0,0,0,0); }
int poweroff(void)      { return reboot(0x4321fedc); }
int fstat(int fd, struct qvos_stat *st) { return (int)syscall6(SYS_fstat,fd,(long)st,0,0,0,0); }
void *sbrk(intptr_t inc){ return (void *)syscall6(SYS_sbrk,inc,0,0,0,0,0); }

/* ---- tiny stdio ---- */
void puts(const char *s)
{
    size_t n = strlen(s);
    write(1, s, n);
    write(1, "\n", 1);
}

static void putdec(long v)
{
    char b[24];
    int i = 23;
    b[i--] = 0;
    int neg = 0;
    if (v < 0) { neg = 1; v = -v; }
    do { b[i--] = (char)('0' + v % 10); v /= 10; } while (v);
    if (neg) b[i--] = '-';
    write(1, &b[i+1], (size_t)(23 - i - 1));
}

static void puthex(unsigned long v)
{
    static const char d[] = "0123456789abcdef";
    char b[19];
    b[0]='0'; b[1]='x'; b[18]=0;
    for (int i=17; i>=2; i--) { b[i]=d[v&15]; v>>=4; }
    write(1, b, 18);
}

static void puts0(const char *s);

static void vprintf(const char *fmt, va_list ap)
{
    for (const char *p = fmt; *p; p++) {
        if (*p != '%') {
            write(1, p, 1);
            continue;
        }
        p++;
        if (*p == '%') { write(1, "%", 1); continue; }
        if (*p == 's') { const char *s = va_arg(ap, const char *); puts0(s); continue; }
        if (*p == 'c') { char c = (char)va_arg(ap, int); write(1, &c, 1); continue; }
        if (*p == 'd') { putdec(va_arg(ap, long)); continue; }
        if (*p == 'x') { puthex(va_arg(ap, unsigned long)); continue; }
        write(1, p, 1);
    }
}

static void puts0(const char *s) { if(s) write(1,s,strlen(s)); }

void printf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
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
