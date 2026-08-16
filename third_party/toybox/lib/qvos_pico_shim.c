/* qvos_pico_shim.c — small POSIX-flavored helpers that toybox expects but
 * picolibc's embedded profile does not provide.  OS syscalls live in
 * user/picostubs.c; this file is toybox-link-only glue.
 *
 * Everything here is intentionally minimal: qvos is single-user, has no
 * symlinks, no block-device numbering and no inotify/kqueue.
 */
#include <errno.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

/* ---- device numbers ------------------------------------------------ */

int dev_major(dev_t dev)   { return (int)(dev >> 16); }
int dev_minor(dev_t dev)   { return (int)(dev & 0xffff); }
dev_t dev_makedev(int maj, int min)
{
    return (dev_t)(((unsigned)maj << 16) | (unsigned)min);
}

/* ---- copy loop (qvos FAT32 is read-only; this is rarely used) ------ */

long long
sendfile_len(int in, int out, long long bytes, long long *consumed)
{
    static char buf[4096];
    long long done = 0;

    while (bytes < 0 || done < bytes) {
        size_t want = sizeof(buf);
        if (bytes >= 0 && (long long)want > bytes - done)
            want = (size_t)(bytes - done);
        ssize_t r = read(in, buf, want);
        if (r <= 0)
            break;
        ssize_t w = write(out, buf, (size_t)r);
        if (w < 0)
            break;
        done += w;
    }
    if (consumed)
        *consumed = done;
    return done;
}

/* ---- xnotify (tail -f backend; no inotify on qvos) ----------------- */

struct xnotify { int dummy; };

struct xnotify *xnotify_init(int max)  { (void)max; return 0; }
int xnotify_add(struct xnotify *n, int fd, char *path)
{ (void)n; (void)fd; (void)path; errno = ENOSYS; return -1; }
int xnotify_wait(struct xnotify *n, char **path)
{ (void)n; (void)path; errno = ENOSYS; return -1; }

/* ---- time parsing (picolibc omits strptime) ------------------------ */

char *
strptime(const char *s, const char *fmt, struct tm *tm)
{
    (void)fmt;
    if (tm)
        memset(tm, 0, sizeof(*tm));
    return (char *)s;
}

/* ---- syslog (stubs: print to stderr) ------------------------------- */

void openlog(const char *ident, int option, int facility)
{ (void)ident; (void)option; (void)facility; }

void
syslog(int priority, const char *fmt, ...)
{
    va_list ap;

    (void)priority;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
}

void
vsyslog(int priority, const char *fmt, va_list ap)
{
    (void)priority;
    vfprintf(stderr, fmt, ap);
}

void closelog(void) {}

/* ---- termios helpers (no tty; keep classic callers compiling) ------ */

int
cfsetspeed(struct termios *tio, speed_t speed)
{
    (void)tio; (void)speed;
    return 0;
}

void
cfmakeraw(struct termios *tio)
{
    if (tio) {
        tio->c_iflag = 0;
        tio->c_oflag = 0;
        tio->c_lflag = 0;
    }
}
