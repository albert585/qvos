#ifndef QVOS_SYSCALL_H
#define QVOS_SYSCALL_H

/* qvos POSIX-subset syscall numbers (kernel/userspace ABI). */
#define SYS_exit        1
#define SYS_fork        2
#define SYS_read        3
#define SYS_write       4
#define SYS_open        5
#define SYS_close       6
#define SYS_waitpid     7
#define SYS_execve      8
#define SYS_lseek       9
#define SYS_getpid      10
#define SYS_sbrk        11
#define SYS_getppid     12
#define SYS_yield       13
#define SYS_nanosleep   14
#define SYS_kill        15
#define SYS_fstat       16
#define SYS_mkdir       17
#define SYS_unlink      18
#define SYS_dup         19
#define SYS_pipe        20
#define SYS_getdents    21
#define SYS_gettimeofday 22
#define SYS_times       23
#define SYS_reboot      24
#define SYS_fbmap       25   /* map shared virtio-gpu framebuffer   */
#define SYS_fbinfo      26   /* copy framebuffer metadata to user   */
#define SYS_fbflush     27   /* transfer + flush the 2D resource    */
#define SYS_fbtext      28   /* draw text with the kernel fb console */
#define SYS_fbclear     29   /* clear screen + reset text cursor    */
#define SYS_mount       30   /* mount a block device on a directory */
#define SYS_chdir       31   /* change current working directory */
#define SYS_getcwd      32   /* copy current working directory to user */

/* Userspace/kernel ABI for SYS_fbinfo. */
struct qvos_fb_info {
    uint32_t width;
    uint32_t height;
    uint32_t bpp;        /* always 32 (B8G8R8X8) */
    uint32_t pitch;      /* bytes per row */
    uint32_t size_bytes;
    uint64_t phys;       /* physical address of the backing store */
};

/* Fixed-size directory entry returned by SYS_getdents.  The syscall fills
 * up to `max` entries and returns the number of entries (0 at EOF). */
#define QVOS_NAME_MAX 64
struct qvos_dirent {
    uint64_t d_ino;      /* FAT start cluster, or devfs slot */
    uint32_t d_type;     /* 0 regular, 1 dir, 2 char, 3 block */
    uint32_t d_namlen;
    char     d_name[QVOS_NAME_MAX];
};

/* One virtio-input event as delivered by /dev/mouse. */
struct qvos_input_event {
    uint16_t type;       /* 0 EV_SYN, 1 EV_KEY, 2 EV_REL, 3 EV_ABS */
    uint16_t code;       /* e.g. ABS_X/ABS_Y/BTN_LEFT */
    uint32_t value;
};

/* SYS_gettimeofday (Unix epoch time from the goldfish RTC). */
struct qvos_timeval {
    uint64_t tv_sec;
    uint64_t tv_usec;
};

/* SYS_times (qvos has no per-process CPU accounting; all zeros). */
struct qvos_tms {
    uint64_t tms_utime;
    uint64_t tms_stime;
    uint64_t tms_cutime;
    uint64_t tms_cstime;
};

#endif
