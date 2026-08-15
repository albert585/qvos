#ifndef QVOS_FILE_H
#define QVOS_FILE_H

#include <stdint.h>
#include <stddef.h>
#include <kernel/fat32.h>

#define FILE_TABLE_SIZE 128

enum file_type {
    FD_NONE = 0,
    FD_CONSOLE,
    FD_FAT,
    FD_PIPE,
};

struct file {
    enum file_type type;
    int ref;
    int readable;
    int writable;
    uint32_t offset;
    struct fat32_file fat;
    void *pipe;          /* struct pipe * for FD_PIPE */
};

int pipe_alloc(int fds[2]);

void file_init(void);
struct file *file_console(int idx);
struct file *file_alloc(void);
struct file *file_dup(struct file *f);
void file_close(struct file *f);

struct file *file_open(const char *path, int flags);
int file_read(struct file *f, void *dst, size_t n);
int file_write(struct file *f, const void *src, size_t n);
int file_lseek(struct file *f, int64_t off, int whence);
int file_stat(struct file *f, uint32_t *size, uint32_t *type);

int fd_alloc(struct file *f);
struct file *fd_get(int fd);
void fd_close(int fd);

#endif
