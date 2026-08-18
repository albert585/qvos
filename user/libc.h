#ifndef QVOS_USER_LIBC_H
#define QVOS_USER_LIBC_H

#include <stddef.h>
#include <stdarg.h>
#include <stdint.h>
#include <kernel/syscall.h>

typedef long ssize_t;
typedef int pid_t;

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
int   open(const char *path, int flags, ...);
int   close(int fd);
ssize_t read(int fd, void *buf, size_t n);
ssize_t write(int fd, const void *buf, size_t n);
int   lseek(int fd, int off, int whence);
int   dup(int fd);
int   pipe(int fds[2]);
int   getdents(int fd, struct qvos_dirent *ents, int max);
int   reboot(int how);
int   poweroff(void);
int   mount(const char *source, const char *target);
int   chdir(const char *path);
char *getcwd(char *buf, size_t size);

/* ---- virtio-gpu framebuffer ---- */
void *fbmap(void);
int   fbinfo(struct qvos_fb_info *info);
int   fbflush(void);
int   fbtext(const char *s, size_t n);
int   fbclear(uint32_t bg);

struct qvos_stat {
    uint64_t st_size;
    uint32_t st_mode;
};
int fstat(int fd, struct qvos_stat *st);

void *sbrk(intptr_t inc);
void *malloc(size_t n);
void  free(void *p);

/* ---- tiny stdio ---- */
typedef struct qvos_FILE {
    int fd;
    int ungot;
    int eof;
    int err;
} FILE;

extern FILE *stdin, *stdout, *stderr;

int printf(const char *fmt, ...);
int fprintf(FILE *f, const char *fmt, ...);
int vprintf(const char *fmt, va_list ap);
int vfprintf(FILE *f, const char *fmt, va_list ap);
int vsnprintf(char *str, size_t size, const char *fmt, va_list ap);
int snprintf(char *str, size_t size, const char *fmt, ...);
int sprintf(char *str, const char *fmt, ...);
int puts(const char *s);
int fputs(const char *s, FILE *f);
int fputc(int c, FILE *f);
int putc(int c, FILE *f);
size_t fwrite(const void *p, size_t size, size_t n, FILE *f);
size_t fread(void *p, size_t size, size_t n, FILE *f);
int fgetc(FILE *f);
char *fgets(char *s, int size, FILE *f);
int fflush(FILE *f);
int ferror(FILE *f);
int feof(FILE *f);
void clearerr(FILE *f);

/* ---- string ---- */
size_t strlen(const char *s);
int strcmp(const char *a, const char *b);
int strncmp(const char *a, const char *b, size_t n);
char *strcpy(char *dst, const char *src);
char *strcat(char *dst, const char *src);
void *memcpy(void *dst, const void *src, size_t n);
void *memset(void *s, int c, size_t n);

#endif
