#ifndef QVOS_KALLOC_H
#define QVOS_KALLOC_H

#include <stddef.h>

void *kmalloc(size_t size);       /* 16-byte aligned general kernel heap */
void *kzalloc(size_t size);       /* zeroed kmalloc                    */
void  kfree(void *ptr);

void *kalloc_page(void);          /* one zeroed page                   */
void  kfree_page(void *page);

void  kalloc_selftest(void);

#endif
