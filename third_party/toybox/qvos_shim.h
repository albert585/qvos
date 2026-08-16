/* Freestanding POSIX-ish shim that lets toybox compile against the qvos
 * libc subset instead of a real libc/sysroot. */
#ifndef QVOS_TOYBOX_SHIM_H
#define QVOS_TOYBOX_SHIM_H

#pragma push_macro("nanosleep")
#undef nanosleep
#pragma push_macro("fstat")
#undef fstat
#include "libc.h"
#pragma pop_macro("fstat")
#pragma pop_macro("nanosleep")
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* ---- basic types toybox expects ---- */
typedef int64_t off_t;
typedef int64_t off64_t;
typedef uint32_t mode_t;
typedef uint32_t uid_t;
typedef uint32_t gid_t;
typedef uint32_t dev_t;
typedef uint64_t ino_t;
typedef uint64_t nlink_t;
typedef int64_t time_t;
typedef int64_t clock_t;
typedef uint32_t useconds_t;
typedef int64_t suseconds_t;
typedef int64_t blksize_t;
typedef int64_t blkcnt_t;
typedef int64_t ssize_t2; /* unused; qvos libc.h already has ssize_t */

typedef struct qvos_DIR {
    int fd;
    int next;
} DIR;

struct dirent {
    uint64_t d_ino;
    unsigned char d_type;
    char d_name[64];
};


struct timespec { long tv_sec; long tv_nsec; };
struct timeval { long tv_sec; long tv_usec; };
struct timezone { int tz_minuteswest, tz_dsttime; };

struct stat {
    mode_t st_mode;
    uint64_t st_size;
    uid_t st_uid;
    gid_t st_gid;
    ino_t st_ino;
    nlink_t st_nlink;
    dev_t st_dev;
    dev_t st_rdev;
    blksize_t st_blksize;
    blkcnt_t st_blocks;
    time_t st_atime;
    time_t st_mtime;
    time_t st_ctime;
    struct timespec st_atim;
    struct timespec st_mtim;
    struct timespec st_ctim;
};

/* qvos libc's fstat is renamed by a compile-time macro for toybox. */
int qvos_fstat(int fd, struct stat *st);

struct statvfs { unsigned long f_bsize, f_frsize, f_blocks, f_bfree, f_bavail, f_files, f_ffree, f_favail, f_fsid, f_flag, f_namemax; };
struct utsname { char sysname[65], nodename[65], release[65], version[65], machine[65], domainname[65]; };
struct tm { int tm_sec, tm_min, tm_hour, tm_mday, tm_mon, tm_year, tm_wday, tm_yday, tm_isdst; };
struct passwd { char *pw_name, *pw_passwd; uid_t pw_uid; gid_t pw_gid; char *pw_gecos, *pw_dir, *pw_shell; };
struct group { char *gr_name, *gr_passwd; gid_t gr_gid; char **gr_mem; };
struct termios { uint32_t c_iflag, c_oflag, c_cflag, c_lflag; char c_line; unsigned char c_cc[32]; };
struct winsize { unsigned short ws_row, ws_col, ws_xpixel, ws_ypixel; };
struct rlimit { uint64_t rlim_cur, rlim_max; };
struct sched_param { int sched_priority; };

typedef uint64_t jmp_buf[32];
typedef uint64_t sigjmp_buf[32];
typedef void (*sighandler_t)(int);
typedef unsigned char sigset_t[16];
typedef struct { void *ss_sp; size_t ss_size; int ss_flags; } stack_t;

struct sigaction {
    union { sighandler_t sa_handler; void (*sa_sigaction)(int, void *, void *); };
    sigset_t sa_mask;
    int sa_flags;
    void (*sa_restorer)(void);
};

struct iovec { void *iov_base; size_t iov_len; };

#define _Noreturn __attribute__((noreturn))
#define printf_format __attribute__((format(printf, 1, 2)))
#define socklen_t uint32_t
#define SWAP_LE64(x) __builtin_bswap64(x)
#define SWAP_BE64(x) (x)
#define SWAP_LE32(x) __builtin_bswap32(x)
#define SWAP_BE32(x) (x)

struct pollfd { int fd; short events, revents; };

double sin(double x);
double cos(double x);
double tan(double x);
double fabs(double x);
double sqrt(double x);
double pow(double x, double y);
double log(double x);
double floor(double x);
double ceil(double x);

/* Minimal regex/network types toybox's headers mention. */
typedef struct { int re_nsub; void *opaque; } regex_t;
typedef struct { int rm_so, rm_eo; } regmatch_t;
struct sockaddr { unsigned short sa_family; char sa_data[14]; };
struct sockaddr_in { unsigned short sin_family; unsigned short sin_port; uint32_t sin_addr; char sin_zero[8]; };
struct sockaddr_in6 { unsigned short sin6_family; unsigned short sin6_port; uint32_t sin6_flowinfo; unsigned char sin6_addr[16]; uint32_t sin6_scope_id; };
struct in_addr { uint32_t s_addr; };
struct hostent { char *h_name; char **h_aliases; int h_addrtype, h_length; char **h_addr_list; };
struct addrinfo { int ai_flags, ai_family, ai_socktype, ai_protocol; size_t ai_addrlen; struct sockaddr *ai_addr; char *ai_canonname; struct addrinfo *ai_next; };

typedef double FLOAT;
#define INT_MAX 2147483647
#define INT_MIN (-2147483647 - 1)
#define LONG_MAX 9223372036854775807L
#define LONG_MIN (-LONG_MAX - 1L)
#define LLONG_MAX 9223372036854775807LL
#define LLONG_MIN (-LLONG_MAX - 1LL)
#define SIZE_MAX (~(size_t)0)
#define PATH_MAX 1024
#define NAME_MAX 255
#define MB_CUR_MAX 1
#define _PATH_DEFPATH "/bin:/usr/bin"

#define AT_FDCWD -100
#define AT_SYMLINK_NOFOLLOW 0x100
#define DT_UNKNOWN 0
#define DT_FIFO 1
#define DT_CHR 2
#define DT_DIR 4
#define DT_BLK 6
#define DT_REG 8
#define DT_LNK 10
#define DT_SOCK 12
#define DT_WHT 14

#define O_ACCMODE 0003
#define IS_BIG_ENDIAN 0
#define F_SETLK 6
#define F_SETLKW 7

#define TIOCGWINSZ 0x5413
#define TIOCNOTTY 0x5422
#define POLLIN 1
#define POLLOUT 4
#define SOL_SOCKET 1
#define SO_REUSEADDR 2
#define AI_PASSIVE 1

#define WIFEXITED(s)   (((s)&0x7f)==0)
#define WEXITSTATUS(s) (((s)>>8)&0xff)
#define WIFSIGNALED(s) (((s)&0x7f)!=0)
#define WTERMSIG(s)    ((s)&0x7f)

#define ISIG 1
#define ICANON 2
#define ECHO 8
#define ECHOE 16
#define ECHOK 32
#define ECHONL 64
#define ECHOCTL 512
#define ECHOKE 2048
#define IEXTEN 32768
#define ICRNL 256
#define IUTF8 16384
#define IXANY 2048
#define OPOST 1
#define ONLCR 4
#define CS8 48
#define CREAD 128

uint32_t bswap_32(uint32_t x);
uint64_t bswap_64(uint64_t x);
char *strerror(int errnum);
size_t strcspn(const char *s, const char *reject);
char *strpbrk(const char *s, const char *accept);
char *strsep(char **sp, const char *delim);
int sscanf(const char *s, const char *fmt, ...);
int openat(int dirfd, const char *path, int flags, ...);
int readlinkat(int dirfd, const char *path, char *buf, size_t size);
int mkdirat(int dirfd, const char *path, mode_t mode);
int fchmodat(int dirfd, const char *path, mode_t mode, int flags);
int fchmod(int fd, mode_t mode);
int fchown(int fd, uid_t uid, gid_t gid);
int fchownat(int dirfd, const char *path, uid_t uid, gid_t gid, int flags);
DIR *fdopendir(int fd);
int rename(const char *old, const char *new);
int execv(const char *path, char *const argv[]);
int execvp(const char *file, char *const argv[]);
pid_t vfork(void);
pid_t xfork(void);
int wcwidth(int wc);
int towlower(int c);
size_t wcrtomb(char *s, wchar_t wc, void *ps);
void cfmakeraw(struct termios *t);
int cfsetspeed(struct termios *t, int speed);
int get_block_device_size(int fd, unsigned long long *size);
int socket(int domain, int type, int protocol);
int bind(int fd, const struct sockaddr *addr, uint32_t len);
int connect(int fd, const struct sockaddr *addr, uint32_t len);
int setsockopt(int fd, int level, int opt, const void *val, uint32_t len);
int getaddrinfo(const char *node, const char *service, const struct addrinfo *hints, struct addrinfo **res);
void freeaddrinfo(struct addrinfo *res);
const char *gai_strerror(int err);
int tolower(int c);
int toupper(int c);
int isalpha(int c);
int isdigit(int c);
int isalnum(int c);
int isspace(int c);
int isprint(int c);
int isupper(int c);
int islower(int c);
int isxdigit(int c);
int ispunct(int c);

/* ---- fd / seek / open constants ---- */
#ifndef O_RDONLY
#define O_RDONLY 0
#define O_WRONLY 1
#define O_RDWR 2
#endif
#define O_CREAT 0100
#define O_EXCL 0200
#define O_TRUNC 01000
#define O_APPEND 02000
#define O_NONBLOCK 04000
#define O_CLOEXEC 02000000
#define O_DIRECTORY 0200000
#define O_NOFOLLOW 0400000

#ifndef SEEK_SET
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
#endif

#ifndef STDIN_FILENO
#define STDIN_FILENO 0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2
#endif

/* ---- stat mode bits ---- */
#define S_IFMT  0170000
#define S_IFSOCK 0140000
#define S_IFLNK 0120000
#define S_IFREG 0100000
#define S_IFBLK 0060000
#define S_IFDIR 0040000
#define S_IFCHR 0020000
#define S_IFIFO 0010000
#define S_ISUID 04000
#define S_ISGID 02000
#define S_ISVTX 01000
#define S_IRWXU 00700
#define S_IRUSR 00400
#define S_IWUSR 00200
#define S_IXUSR 00100
#define S_IRWXG 00070
#define S_IRWXO 00007
#define S_ISREG(m) (((m)&S_IFMT)==S_IFREG)
#define S_ISDIR(m) (((m)&S_IFMT)==S_IFDIR)
#define S_ISCHR(m) (((m)&S_IFMT)==S_IFCHR)
#define S_ISBLK(m) (((m)&S_IFMT)==S_IFBLK)
#define S_ISLNK(m) (((m)&S_IFMT)==S_IFLNK)
#define S_ISFIFO(m) (((m)&S_IFMT)==S_IFIFO)
#define S_ISSOCK(m) (((m)&S_IFMT)==S_IFSOCK)

/* ---- errno ---- */
extern int errno;
#define EPERM 1
#define ENOENT 2
#define ESRCH 3
#define EINTR 4
#define EIO 5
#define ENXIO 6
#define E2BIG 7
#define ENOEXEC 8
#define EBADF 9
#define ECHILD 10
#define EAGAIN 11
#define ENOMEM 12
#define EACCES 13
#define EFAULT 14
#define ENOTBLK 15
#define EBUSY 16
#define EEXIST 17
#define EXDEV 18
#define ENODEV 19
#define ENOTDIR 20
#define EISDIR 21
#define EINVAL 22
#define ENFILE 23
#define EMFILE 24
#define ENOTTY 25
#define ETXTBSY 26
#define EFBIG 27
#define ENOSPC 28
#define ESPIPE 29
#define EROFS 30
#define EMLINK 31
#define EPIPE 32
#define EDOM 33
#define ERANGE 34
#define ENOSYS 38
#define ENOTEMPTY 39
#define ELOOP 40
#define EWOULDBLOCK EAGAIN
#define ENOTSUP 95

/* ---- signals (small subset) ---- */
#define SIGHUP 1
#define SIGINT 2
#define SIGQUIT 3
#define SIGILL 4
#define SIGTRAP 5
#define SIGABRT 6
#define SIGBUS 7
#define SIGFPE 8
#define SIGKILL 9
#define SIGUSR1 10
#define SIGSEGV 11
#define SIGUSR2 12
#define SIGPIPE 13
#define SIGALRM 14
#define SIGTERM 15
#define SIGCHLD 17
#define SIGCONT 18
#define SIGSTOP 19
#define SIGTSTP 20
#define SIGTTIN 21
#define SIGTTOU 22
#define SIG_DFL ((sighandler_t)0)
#define SIG_IGN ((sighandler_t)1)
#define SIG_ERR ((sighandler_t)-1)

/* ---- stdio ---- */
#ifndef NULL
#define NULL ((void *)0)
#endif
#ifndef EOF
#define EOF (-1)
#endif
extern FILE *stdin, *stdout, *stderr;
#define stdin stdin
#define stdout stdout
#define stderr stderr

int fflush(FILE *f);
int fputc(int c, FILE *f);
int putc(int c, FILE *f);
int putchar(int c);
int fputs(const char *s, FILE *f);
int puts(const char *s);
size_t fwrite(const void *p, size_t size, size_t nmemb, FILE *f);
size_t fread(void *p, size_t size, size_t nmemb, FILE *f);
int fgetc(FILE *f);
int getc(FILE *f);
int getchar(void);
char *fgets(char *s, int size, FILE *f);
ssize_t getline(char **lineptr, size_t *n, FILE *f);
ssize_t getdelim(char **lineptr, size_t *n, int delim, FILE *f);
FILE *fopen(const char *path, const char *mode);
FILE *fdopen(int fd, const char *mode);
int fclose(FILE *f);
int ferror(FILE *f);
int feof(FILE *f);
void clearerr(FILE *f);
void rewind(FILE *f);
int fseek(FILE *f, long off, int whence);
long ftell(FILE *f);
int setvbuf(FILE *f, char *buf, int mode, size_t size);
int printf(const char *fmt, ...);
int fprintf(FILE *f, const char *fmt, ...);
int vprintf(const char *fmt, va_list ap);
int vfprintf(FILE *f, const char *fmt, va_list ap);
int vsnprintf(char *str, size_t size, const char *fmt, va_list ap);
int snprintf(char *str, size_t size, const char *fmt, ...);
int sprintf(char *str, const char *fmt, ...);
int vasprintf(char **strp, const char *fmt, va_list ap);
int asprintf(char **strp, const char *fmt, ...);
void perror(const char *s);

/* ---- stdlib / string additions ---- */
void exit(int status) __attribute__((noreturn));
void _exit(int status) __attribute__((noreturn));
void *calloc(size_t n, size_t size);
void *realloc(void *p, size_t n);
void *reallocarray(void *p, size_t n, size_t size);
char *strdup(const char *s);
char *strndup(const char *s, size_t n);
size_t strnlen(const char *s, size_t max);
char *strchr(const char *s, int c);
char *strrchr(const char *s, int c);
char *strstr(const char *h, const char *n);
char *strcasestr(const char *h, const char *n);
int strcasecmp(const char *a, const char *b);
int strncasecmp(const char *a, const char *b, size_t n);
int memcmp(const void *a, const void *b, size_t n);
void *memmove(void *dst, const void *src, size_t n);
void *memchr(const void *s, int c, size_t n);
char *strtok(char *s, const char *delim);
char *strtok_r(char *s, const char *delim, char **save);
long strtol(const char *s, char **end, int base);
long long strtoll(const char *s, char **end, int base);
unsigned long strtoul(const char *s, char **end, int base);
unsigned long long strtoull(const char *s, char **end, int base);
double strtod(const char *s, char **end);
int atoi(const char *s);
long atol(const char *s);
long long atoll(const char *s);
char *getenv(const char *name);
int setenv(const char *name, const char *value, int overwrite);
int unsetenv(const char *name);
void qsort(void *base, size_t nmemb, size_t size, int (*cmp)(const void *, const void *));
void srandom(unsigned seed);
long random(void);
int rand(void);
void srand(unsigned seed);
int abs(int x);
long labs(long x);
long long llabs(long long x);

/* ---- unistd / process / files ---- */
int dup2(int oldfd, int newfd);
int isatty(int fd);
char *getcwd(char *buf, size_t size);
int chdir(const char *path);
int access(const char *path, int mode);
int unlink(const char *path);
int link(const char *a, const char *b);
int symlink(const char *a, const char *b);
ssize_t readlink(const char *path, char *buf, size_t size);
int fsync(int fd);
int ftruncate(int fd, off_t len);
int truncate(const char *path, off_t len);
int mkdir(const char *path, mode_t mode);
int rmdir(const char *path);
unsigned sleep(unsigned seconds);
int usleep(useconds_t usec);
int gettimeofday(struct timeval *tv, struct timezone *tz);
long sysconf(int name);
pid_t getppid(void);
int getuid(void);
int geteuid(void);
int getgid(void);
int getegid(void);
int setpgid(pid_t pid, pid_t pgid);
pid_t getpgrp(void);
int kill(pid_t pid, int sig);
int ioctl(int fd, unsigned long req, ...);
int fcntl(int fd, int cmd, ...);
int tcgetattr(int fd, struct termios *t);
int tcsetattr(int fd, int opt, const struct termios *t);
int uname(struct utsname *u);
int nice(int inc);
int getpriority(int which, int who);
int setpriority(int which, int who, int prio);

/* ---- dirent ---- */
DIR *opendir(const char *path);
struct dirent *readdir(DIR *d);
int closedir(DIR *d);
void rewinddir(DIR *d);
int dirfd(DIR *d);

/* ---- stat / mmap ---- */
int stat(const char *path, struct stat *st);
int lstat(const char *path, struct stat *st);
int fstatat(int dirfd, const char *path, struct stat *st, int flags);
int statfs(const char *path, struct statvfs *st);
int fstatfs(int fd, struct statvfs *st);
void *mmap(void *addr, size_t len, int prot, int flags, int fd, off_t off);
int munmap(void *addr, size_t len);
int mprotect(void *addr, size_t len, int prot);

/* ---- signals / setjmp ---- */
int setjmp(jmp_buf env);
void longjmp(jmp_buf env, int val) __attribute__((noreturn));
int sigsetjmp(jmp_buf env, int savesigs);
void siglongjmp(jmp_buf env, int val) __attribute__((noreturn));
sighandler_t signal(int sig, sighandler_t h);
int sigaction(int sig, const void *act, void *old);
int sigemptyset(void *set);
int sigfillset(void *set);
int sigaddset(void *set, int sig);
int sigprocmask(int how, const void *set, void *old);

/* ---- time ---- */
time_t time(time_t *t);
clock_t clock(void);
long clock_gettime(int clk, struct timespec *ts);
char *ctime(const time_t *t);
char *asctime(const struct tm *tm);
struct tm *localtime(const time_t *t);
struct tm *gmtime(const time_t *t);
time_t mktime(struct tm *tm);
size_t strftime(char *s, size_t max, const char *fmt, const struct tm *tm);

/* ---- strings.h / ctype ---- */
#define _GNU_SOURCE 1
extern char **environ;
int isatty(int fd);

/* ---- extra stubs the toybox core asks for ---- */
#define MAP_FAILED ((void *)-1)
#define MAP_SHARED 1
#define MAP_PRIVATE 2
#define PROT_READ 1
#define PROT_WRITE 2
#define PROT_EXEC 4
#define UINT_MAX 0xffffffffU
#define NSIG 65
#define SIG_BLOCK 0
#define SIG_UNBLOCK 1
#define SIGWINCH 28
#define TCSAFLUSH 2
#define O_PATH 010000000
#define _IOFBF 0
#define _IOLBF 1
#define _IONBF 2
#define LC_CTYPE 0
#define LC_CTYPE_MASK 1
#define CODESET 14

int qvos_nanosleep(const void *req, const void *rem);
char *stpcpy(char *dst, const char *src);
int mkstemp(char *tmpl);
pid_t setsid(void);
int chroot(const char *path);
int umask(mode_t mask);
int setuid(uid_t uid);
int setgid(gid_t gid);
int seteuid(uid_t uid);
int setegid(gid_t gid);
char *basename(char *path);
char *dirname(char *path);
char *setlocale(int cat, const char *loc);
char *nl_langinfo(int item);
void *newlocale(int mask, const char *loc, void *base);
void *uselocale(void *loc);
struct passwd *getpwnam(const char *name);
struct passwd *getpwuid(uid_t uid);
struct group *getgrnam(const char *name);
struct group *getgrgid(gid_t gid);
int getpwnam_r(const char *n, struct passwd *p, char *b, size_t s, struct passwd **r);
int getpwuid_r(uid_t u, struct passwd *p, char *b, size_t s, struct passwd **r);
int getgrnam_r(const char *n, struct group *g, char *b, size_t s, struct group **r);
int getgrgid_r(gid_t u, struct group *g, char *b, size_t s, struct group **r);
int regexec(const regex_t *re, const char *s, size_t n, regmatch_t m[], int flags);
void xgetrandom(void *buf, unsigned len);
void xsignal_all_killers(void *handler);
char *num_to_sig(int sig);

#define REG_EXTENDED 1
#define REG_STARTEND 2
#define CLOCK_MONOTONIC 1
#define LOG_DAEMON (3<<3)
#define LOG_AUTH (4<<3)
#define LOG_PID 1
int dev_major(dev_t dev);
int dev_minor(dev_t dev);
dev_t dev_makedev(int major, int minor);
struct tm *localtime_r(const time_t *t, struct tm *tm);
char *strptime(const char *s, const char *fmt, struct tm *tm);
int initgroups(const char *user, gid_t group);
int regcomp(regex_t *re, const char *pat, int flags);
size_t regerror(int err, const regex_t *re, char *buf, size_t size);
void tzset(void);
ssize_t writev(int fd, const struct iovec *iov, int iovcnt);
void openlog(const char *ident, int option, int facility);
void syslog(int priority, const char *fmt, ...);
void vsyslog(int priority, const char *fmt, va_list ap);
void closelog(void);
struct xnotify;
struct xnotify *xnotify_init(int max);
int xnotify_add(struct xnotify *n, int fd, char *path);
int xnotify_wait(struct xnotify *n, char **path);

#endif
