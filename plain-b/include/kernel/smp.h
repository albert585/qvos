#ifndef QVOS_SMP_H
#define QVOS_SMP_H

#include <stdint.h>
#include <stddef.h>
#include <trapframe.h>
#include <kernel/context.h>

#define QVOS_MAX_HARTS 8
#define QVOS_NUM_HARTS 2

struct proc;

struct hart {
    uint64_t id;
    uint64_t ticks;
    struct proc *proc;             /* currently scheduled process (or 0) */
    struct context sched_ctx;      /* this hart's scheduler context        */
    struct trapframe kern_tf;   /* traps taken while in S-mode land here */
    struct trapframe user_tf;   /* traps taken from U-mode land here      */
} __attribute__((aligned(16)));

extern struct hart harts[QVOS_MAX_HARTS];

#define container_of(ptr, type, member) \
    ((type *)((char *)(ptr) - offsetof(type, member)))

/* Reads the tp register; tp == &harts[hartid].kern_tf while in the kernel. */
struct hart *hart_current(void);

/* Returns the hart that owns a user trapframe. */
struct hart *hart_from_user_tf(struct trapframe *tf);

void smp_init(uint64_t boot_hartid);
void smp_start_secondary_harts(void);
void smp_selftest(void);
uint64_t smp_selftest_finish(void);

/* C entry point for hart 1.. when OpenSBI HSM starts them. */
__attribute__((noreturn)) void secondary_main(uint64_t hartid);

#endif
