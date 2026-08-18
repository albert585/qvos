#ifndef QVOS_TRAPFRAME_H
#define QVOS_TRAPFRAME_H

#include <stdint.h>
#include <stddef.h>

/* Vector-register save area covers VLEN up to QVOS_MAX_VLENB*8 bits.
 * On a machine with a larger VLEN the boot code must refuse to start.
 */
#define QVOS_MAX_VLENB 256
#define QVOS_TF_SIZE   0x4000UL   /* 16 KiB, fixed reservation (see trap.S) */

struct trapframe {
    uint64_t gpr[32];             /* x0..x31, x0 unused         */
    uint64_t sepc;
    uint64_t sstatus;
    uint64_t scause;
    uint64_t stval;
    uint64_t f[32];               /* f0..f31                    */
    uint64_t fcsr;
    uint64_t vcsr;
    uint64_t vl;
    uint64_t vtype;
    uint64_t vstart;
    uint8_t  v[32 * QVOS_MAX_VLENB];   /* v0..v31, whole-register */
    uint64_t kernel_sp;                /* per-process kernel stack top     */
    uint64_t kernel_tp;                /* &current_hart.kern_tf, re-armed  */
    uint64_t satp;                     /* user page table for trap_return  */
} __attribute__((aligned(16)));

_Static_assert(sizeof(struct trapframe) <= QVOS_TF_SIZE,
               "trapframe outgrew the fixed 16 KiB trap reservation");

#define TF_GPR(n)  (offsetof(struct trapframe, gpr) + (n) * 8)
#define TF_SEPC    offsetof(struct trapframe, sepc)
#define TF_SSTATUS offsetof(struct trapframe, sstatus)
#define TF_SCAUSE  offsetof(struct trapframe, scause)
#define TF_STVAL   offsetof(struct trapframe, stval)
#define TF_F       offsetof(struct trapframe, f)
#define TF_FCSR    offsetof(struct trapframe, fcsr)
#define TF_VCSR    offsetof(struct trapframe, vcsr)
#define TF_VL      offsetof(struct trapframe, vl)
#define TF_VTYPE   offsetof(struct trapframe, vtype)
#define TF_VSTART  offsetof(struct trapframe, vstart)
#define TF_V       offsetof(struct trapframe, v)
#define TF_KSTACK  offsetof(struct trapframe, kernel_sp)
#define TF_KTP     offsetof(struct trapframe, kernel_tp)
#define TF_SATP    offsetof(struct trapframe, satp)

struct trapframe *trap_dispatch(struct trapframe *tf);
void trap_init_percpu(void);
void trap_enable_timer(uint64_t interval_us);
void trap_return(struct trapframe *tf) __attribute__((noreturn));

#endif
