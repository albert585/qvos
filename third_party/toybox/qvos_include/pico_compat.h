/* pico_compat.h — preincluded for toybox builds against picolibc.
 * picolibc's embedded profile omits a few Linux-shaped headers/defs
 * that toybox's portability layer assumes exist. */
#ifndef QVOS_PICO_COMPAT_H
#define QVOS_PICO_COMPAT_H

#include <stddef.h>
#include <sys/types.h>

/* toybox millitime() uses the POSIX clock ids. */
#ifndef CLOCK_REALTIME
#define CLOCK_REALTIME 0
#endif
#ifndef CLOCK_MONOTONIC
#define CLOCK_MONOTONIC 1
#endif

/* picolibc ioctl header only defines TIOCGWINSZ. */
#ifndef TIOCNOTTY
#define TIOCNOTTY 0x5422
#endif

/* picolibc omits strptime; toybox's parse helpers declare/use it. */
struct tm;
char *strptime(const char *, const char *, struct tm *);
int wcwidth(wchar_t);

/* toybox cp.c references the *at-less xattr family unconditionally;
 * picolibc has no <sys/xattr.h>. */
ssize_t xattr_flist(int, char *, size_t);
ssize_t xattr_fget(int, const char *, void *, size_t);
ssize_t xattr_fset(int, const char *, const void *, size_t, int);

/* picolibc termios is trimmed; declare the classic helpers toybox uses. */
typedef unsigned int speed_t;
struct termios;
int cfsetspeed(struct termios *, speed_t);
void cfmakeraw(struct termios *);
#ifndef IUTF8
#define IUTF8   0x4000
#endif
#ifndef ECHOCTL
#define ECHOCTL 0x0200
#endif
#ifndef ECHOKE
#define ECHOKE  0x0800
#endif

/* picolibc omits the utimensat special values. */
#ifndef UTIME_NOW
#define UTIME_NOW  ((1L << 30) - 1L)
#endif
#ifndef UTIME_OMIT
#define UTIME_OMIT ((1L << 30) - 2L)
#endif

/* picolibc paths.h has /bin/sh but no default PATH. */
#ifndef _PATH_DEFPATH
#define _PATH_DEFPATH "/bin:/usr/bin"
#endif

/* toybox/lib/portability.h uses struct statfs by value-less pointer. */
struct statfs {
    long f_type;
    long f_bsize;
    long f_blocks;
    long f_bfree;
    long f_bavail;
    long f_files;
    long f_ffree;
    long f_fsid;
    long f_namelen;
    long f_frsize;
    long f_flags;
    long f_spare[4];
};

#endif
