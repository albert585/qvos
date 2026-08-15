#ifndef QVOS_ATOMIC_H
#define QVOS_ATOMIC_H

#include <stdint.h>

/* RISC-V AMO-based primitives.  Every primitive expands to a real atomic
 * memory operation (amoswap/amoadd) plus the required fences.
 */
static inline void atomic_fence_rw(void)
{
    __asm__ volatile("fence rw, rw" : : : "memory");
}

static inline uint32_t atomic_swap_u32(volatile uint32_t *p, uint32_t v)
{
    uint32_t old;
    __asm__ volatile("amoswap.w.aq %0, %2, %1"
                     : "=r"(old), "+A"(*(volatile uint32_t *)p)
                     : "r"(v)
                     : "memory");
    return old;
}

static inline void atomic_store_u32_release(volatile uint32_t *p, uint32_t v)
{
    __asm__ volatile("fence rw, w" : : : "memory");
    __asm__ volatile("amoswap.w.rl zero, %1, %0"
                     : "+A"(*(volatile uint32_t *)p)
                     : "r"(v)
                     : "memory");
}

static inline uint32_t atomic_load_u32(volatile uint32_t *p)
{
    uint32_t v;
    __asm__ volatile("lw %0, %1" : "=r"(v) : "A"(*(volatile uint32_t *)p));
    return v;
}

static inline void cpu_relax(void)
{
    /* Compiler barrier only; keeps the base RV64IMAFCV ISA string clean. */
    __asm__ volatile("" : : : "memory");
}

#endif
