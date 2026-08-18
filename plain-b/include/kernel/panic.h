#ifndef QVOS_PANIC_H
#define QVOS_PANIC_H

#include <stdint.h>

__attribute__((noreturn)) void panic(const char *msg, uint64_t cause,
                                     uint64_t stval, uint64_t sepc);

#endif
