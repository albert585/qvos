#ifndef QVOS_VIRTIOSND_H
#define QVOS_VIRTIOSND_H

#include <stdint.h>
#include <stddef.h>

int  snd_init(void);
int  snd_active(void);
int  snd_write(const void *buf, size_t n);

#endif
