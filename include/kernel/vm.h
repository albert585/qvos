#ifndef QVOS_VM_H
#define QVOS_VM_H

#include <stdint.h>
#include <stddef.h>

#define PAGE_SIZE      4096UL
#define PAGE_SHIFT     12
#define PAGE_MASK      (PAGE_SIZE - 1)

#define PTE_V (1UL << 0)
#define PTE_R (1UL << 1)
#define PTE_W (1UL << 2)
#define PTE_X (1UL << 3)
#define PTE_U (1UL << 4)
#define PTE_G (1UL << 5)
#define PTE_A (1UL << 6)
#define PTE_D (1UL << 7)

#define SATP_SV39 (8UL << 60)
#define SATP_MODE_MASK (0xfUL << 60)

#define PTE2PA(pte)  (((pte) >> 10) << 12)
#define PA2PTE(pa)   (((pa) >> 12) << 10)

#define USER_VA_BASE   0x0000000000000000UL
#define USER_VA_TOP    0x0000004000000000UL   /* 256 GiB user space */
#define USER_STACK_TOP (USER_VA_TOP - 0x100000UL)
#define TRAPFRAME_VA   0x0000000020000000UL   /* 4 pages, per-process */

typedef uint64_t pte_t;
typedef uint64_t *pagetable_t;

void  vm_init(void);
void  vm_enable_hart(void);
void  uvm_map_common(pagetable_t pg, void *trapframe);
int   vm_copy_user(pagetable_t oldpg, pagetable_t newpg);
void  vm_free_user_pages(pagetable_t pg);

pagetable_t vm_create(void);
void  vm_free(pagetable_t pg);
pte_t *vm_walk(pagetable_t pg, uint64_t va, int alloc);
pte_t *vm_walk(pagetable_t pg, uint64_t va, int alloc);
int   vm_map(pagetable_t pg, uint64_t va, uint64_t pa, uint64_t size,
             uint64_t perm);
uint64_t vm_translate(pagetable_t pg, uint64_t va);
uint64_t vm_flags(pagetable_t pg, uint64_t va);

void vm_selftest(void);

extern pagetable_t kernel_pagetable;

#endif
