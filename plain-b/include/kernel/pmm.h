#ifndef QVOS_PMM_H
#define QVOS_PMM_H

#include <stdint.h>
#include <stddef.h>

/* Physical page allocator (Stage 4): free-list over DRAM above the kernel
 * image.  Every returned page is zeroed. */
void pmm_init(uint64_t dtb_pa);
void *pmm_alloc_page(void);
void  pmm_free_page(void *page);
void *pmm_alloc_pages(size_t n);       /* n contiguous zeroed pages */
void  pmm_free_pages(void *first, size_t n);
uint64_t pmm_phys(void *ptr);
void pmm_report(void);

#endif
