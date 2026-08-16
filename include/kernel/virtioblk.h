#ifndef QVOS_VIRTIOBLK_H
#define QVOS_VIRTIOBLK_H

#include <stdint.h>
#include <stddef.h>

/* virtio-blk block-device abstraction, exposed to userspace as /dev/vda. */
int      vda_init(void);        /* 0 on success, -1 if no device found */
int      vda_active(void);
uint64_t vda_capacity(void);    /* bytes */
uint32_t vda_blk_size(void);    /* logical block size, normally 512 */
int      vda_read(void *dst, uint64_t offset, size_t n);
int      vda_write(const void *src, uint64_t offset, size_t n);

#endif
