#ifndef QVOS_CONTEXT_H
#define QVOS_CONTEXT_H

#include <stdint.h>

/* Callee-saved kernel context switched by arch/riscv64/swtch.S. */
struct context {
    uint64_t ra;
    uint64_t sp;
    uint64_t s0, s1, s2, s3, s4, s5, s6, s7, s8, s9, s10, s11;
};

#endif
