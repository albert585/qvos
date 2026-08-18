#ifndef QVOS_SPINLOCK_H
#define QVOS_SPINLOCK_H

#include <stdint.h>
#include <atomic.h>

struct spinlock {
    uint32_t locked;
};

typedef struct spinlock spinlock_t;

void spin_lock(spinlock_t *l);
void spin_unlock(spinlock_t *l);

/* Acquire with interrupts disabled; returns the previous sstatus.SIE state
 * packed into the low bit so it can be restored by spin_unlock_irqrestore. */
uint64_t spin_lock_irqsave(spinlock_t *l);
void     spin_unlock_irqrestore(spinlock_t *l, uint64_t flags);

#endif
