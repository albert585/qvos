#ifndef QVOS_CSR_H
#define QVOS_CSR_H

#include <stdint.h>

/* ---- generic CSR access (csr argument must be a register NAME) ---- */
#define csr_read(csr)                                             \
    ({                                                            \
        uint64_t __v;                                             \
        __asm__ volatile("csrr %0, " #csr : "=r"(__v) : : "memory"); \
        __v;                                                      \
    })

#define csr_write(csr, val)                                       \
    do {                                                          \
        uint64_t __v = (uint64_t)(val);                           \
        __asm__ volatile("csrw " #csr ", %0" : : "r"(__v) : "memory"); \
    } while (0)

#define csr_set(csr, bits)                                        \
    do {                                                          \
        uint64_t __v = (uint64_t)(bits);                          \
        __asm__ volatile("csrs " #csr ", %0" : : "r"(__v) : "memory"); \
    } while (0)

#define csr_clear(csr, bits)                                      \
    do {                                                          \
        uint64_t __v = (uint64_t)(bits);                          \
        __asm__ volatile("csrc " #csr ", %0" : : "r"(__v) : "memory"); \
    } while (0)

/* ---- supervisor CSRs used by qvos ---- */
static inline uint64_t r_sstatus(void)   { return csr_read(sstatus); }
static inline void     w_sstatus(uint64_t v) { csr_write(sstatus, v); }
static inline uint64_t r_sie(void)       { return csr_read(sie); }
static inline void     w_sie(uint64_t v) { csr_write(sie, v); }
static inline uint64_t r_sip(void)       { return csr_read(sip); }
static inline void     w_sip(uint64_t v) { csr_write(sip, v); }
static inline uint64_t r_stvec(void)     { return csr_read(stvec); }
static inline void     w_stvec(uint64_t v) { csr_write(stvec, v); }
static inline uint64_t r_sscratch(void)  { return csr_read(sscratch); }
static inline void     w_sscratch(uint64_t v) { csr_write(sscratch, v); }
static inline uint64_t r_scause(void)    { return csr_read(scause); }
static inline uint64_t r_stval(void)     { return csr_read(stval); }
static inline uint64_t r_sepc(void)      { return csr_read(sepc); }
static inline void     w_sepc(uint64_t v) { csr_write(sepc, v); }
static inline uint64_t r_satp(void)      { return csr_read(satp); }
static inline void     w_satp(uint64_t v) { csr_write(satp, v); }
static inline uint64_t r_stimecmp(void) { return csr_read(stimecmp); }
static inline void     w_stimecmp(uint64_t v) { csr_write(stimecmp, v); }
static inline uint64_t r_vlenb(void)     { return csr_read(vlenb); }

static inline void sfence_vma(void)
{
    __asm__ volatile("sfence.vma zero, zero" : : : "memory");
}

/* ---- sstatus field bits ---- */
#define SSTATUS_SIE   (1UL << 1)
#define SSTATUS_SPIE  (1UL << 5)
#define SSTATUS_SPP   (1UL << 8)
#define SSTATUS_VS    (0x3UL << 9)   /* VS[1:0]  */
#define SSTATUS_FS    (0x3UL << 13)  /* FS[1:0]  */
#define SSTATUS_XS    (0x3UL << 15)
#define SSTATUS_SUM   (1UL << 18)

#define SSTATUS_VS_INITIAL (1UL << 9)
#define SSTATUS_FS_INITIAL (1UL << 13)

/* ---- sie/sip field bits ---- */
#define SIE_SSIE (1UL << 1)
#define SIE_STIE (1UL << 5)
#define SIE_SEIE (1UL << 9)

/* ---- scause ---- */
#define SCAUSE_INTERRUPT (1UL << 63)
#define SCAUSE_CODE(x)   ((x) & ~SCAUSE_INTERRUPT)

#endif
