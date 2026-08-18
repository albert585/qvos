#ifndef QVOS_KLOG_H
#define QVOS_KLOG_H

#include <stdint.h>
#include <stddef.h>

void klog_puts(const char *s);
void klog_hex(uint64_t v);
void klog_write(const char *s, size_t n);
void klog_init(void);

#endif
