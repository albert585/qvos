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

#endif
