#ifndef QVOS_FILE_H
#define QVOS_FILE_H

#include <stdint.h>
#include <stddef.h>
#include <kernel/fat32.h>
#include <kernel/syscall.h>

#define FILE_TABLE_SIZE 128
#define MOUNT_MAX 8

struct mount {
    char path[128];
    struct fat32_fs fs;
    int used;
};

enum file_type {
    FD_NONE = 0,
    FD_CONSOLE,
    FD_FAT,
    FD_PIPE,
    FD_DEVFB,      /* /dev/fb0: raw virtio-gpu framebuffer bytes */
    FD_DEVVDA,     /* /dev/vda: raw virtio-blk disk bytes */
    FD_DEVMOUSE,   /* /dev/mouse: virtio-input event stream */
    FD_DEVKBD,     /* /dev/kbd: virtio-keyboard event stream */
    FD_DEVNULL,    /* /dev/null: discard writes, EOF reads */
    FD_DEVZERO,    /* /dev/zero: infinite zero bytes */
    FD_DEVDIR,     /* /dev: devfs directory */
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

int vfs_mount(const char *source, const char *target);
int vfs_chdir(const char *path, char *out, size_t outsz);

void file_init(void);
struct file *file_console(int idx);
struct file *file_alloc(void);
struct file *file_dup(struct file *f);
void file_close(struct file *f);

struct file *file_open(const char *path, int flags);
int file_read(struct file *f, void *dst, size_t n);
int file_write(struct file *f, const void *src, size_t n);
int file_lseek(struct file *f, int64_t off, int whence);
int file_stat(struct file *f, uint64_t *size, uint32_t *type);
int file_readdir(struct file *f, uint32_t index, struct qvos_dirent *de);

int fd_alloc(struct file *f);
struct file *fd_get(int fd);
void fd_close(int fd);

#endif
