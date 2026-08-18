/* Test-and-set spinlock built on amoswap.w.  The irqsave variant keeps the
 * timer interrupt from deadlocking against a same-hart lock holder.
 */
#include <kernel/spinlock.h>
#include <csr.h>

void spin_lock(spinlock_t *l)
{
    while (atomic_swap_u32(&l->locked, 1) != 0) {
        while (atomic_load_u32(&l->locked) != 0)
            cpu_relax();
    }
    atomic_fence_rw();
}

void spin_unlock(spinlock_t *l)
{
    atomic_store_u32_release(&l->locked, 0);
}

uint64_t spin_lock_irqsave(spinlock_t *l)
{
    uint64_t sstatus = csr_read(sstatus);
    uint64_t flags = !!(sstatus & SSTATUS_SIE);   /* low bit = previous SIE */

    csr_clear(sstatus, SSTATUS_SIE);
    spin_lock(l);
    return flags;
}

void spin_unlock_irqrestore(spinlock_t *l, uint64_t flags)
{
    spin_unlock(l);
    if (flags & 1)
        csr_set(sstatus, SSTATUS_SIE);
}
