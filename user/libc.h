#ifndef QVOS_USER_LIBC_H
#define QVOS_USER_LIBC_H

#include <stddef.h>
#include <stdint.h>
#include <kernel/syscall.h>

typedef long ssize_t;
typedef long pid_t;

/* ---- syscall wrappers ---- */
long  syscall6(long n, long a0, long a1, long a2, long a3, long a4, long a5);
pid_t fork(void);
int   waitpid(pid_t pid, int *status, int options);
int   execve(const char *path, char *const argv[], char *const envp[]);
void  exit(int status) __attribute__((noreturn));
pid_t getpid(void);
pid_t getppid(void);
int   yield(void);
int   nanosleep(void);
int   open(const char *path, int flags);
int   close(int fd);
ssize_t read(int fd, void *buf, size_t n);
ssize_t write(int fd, const void *buf, size_t n);
int   lseek(int fd, int off, int whence);
int   dup(int fd);
int   pipe(int fds[2]);
int   reboot(int how);
int   poweroff(void);

struct qvos_stat {
    uint64_t st_size;
    uint32_t st_mode;
};
int fstat(int fd, struct qvos_stat *st);

void *sbrk(intptr_t inc);
void *malloc(size_t n);
void  free(void *p);

/* ---- tiny stdio ---- */
void puts(const char *s);
void printf(const char *fmt, ...);

/* ---- string ---- */
size_t strlen(const char *s);
int strcmp(const char *a, const char *b);
int strncmp(const char *a, const char *b, size_t n);
char *strcpy(char *dst, const char *src);
char *strcat(char *dst, const char *src);
void *memcpy(void *dst, const void *src, size_t n);
void *memset(void *s, int c, size_t n);

#endif
