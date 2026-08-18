/* Per-hart data and secondary-hart bring-up.
 *
 * tp is the per-hart pointer: while a hart executes kernel code
 * tp == &harts[id].kern_tf.  User tp is preserved through traps in the
 * trapframe and sscratch (see arch/riscv64/trap.S).
 */
#include <kernel/klog.h>
#include <kernel/proc.h>
#include <kernel/sbi.h>
#include <kernel/smp.h>
#include <kernel/spinlock.h>
#include <kernel/vm.h>
#include <csr.h>
#include <trapframe.h>
#include <atomic.h>

extern void secondary_entry(void);
extern char boot_stack_top[];

struct hart harts[QVOS_MAX_HARTS];
static volatile uint32_t secondary_ready;
static volatile uint32_t secondary_test_done;

static spinlock_t smp_test_lock;
static uint64_t   smp_test_counter;

struct hart *hart_current(void)
{
    uint64_t tp;
    __asm__ volatile("mv %0, tp" : "=r"(tp));
    return container_of((void *)tp, struct hart, kern_tf);
}

struct hart *hart_from_user_tf(struct trapframe *tf)
{
    return container_of(tf, struct hart, user_tf);
}

void smp_init(uint64_t hartid)
{
    struct hart *h = &harts[hartid];

    h->id = hartid;
    h->ticks = 0;
    h->proc = 0;
    h->kern_tf.kernel_sp =
        (uint64_t)boot_stack_top - hartid * 0x10000;
    h->kern_tf.kernel_tp = (uint64_t)h->kern_tf.gpr;
    __asm__ volatile("mv tp, %0" : : "r"(h->kern_tf.gpr));
}

void smp_start_secondary_harts(void)
{
    uint64_t boot = hart_current()->id;
    uint64_t err;

    for (uint64_t id = 0; id < QVOS_NUM_HARTS; id++) {
        if (id == boot)
            continue;
        err = sbi_hart_start(id, (uint64_t)secondary_entry, 0);
        if (err != SBI_SUCCESS) {
            klog_puts("[qvos] sbi_hart_start(");
            klog_hex(id);
            klog_puts(") failed: ");
            klog_hex(err);
            klog_puts("\n");
        } else {
            klog_puts("[qvos] hart ");
            klog_hex(id);
            klog_puts(" start request sent\n");
        }
    }

    /* Wait until every requested secondary hart has finished its local
     * trap/CSR setup. */
    while (atomic_load_u32((volatile uint32_t *)&secondary_ready) == 0)
        cpu_relax();
    atomic_fence_rw();
    klog_puts("[qvos] all harts online\n");
}

void smp_selftest(void)
{
    for (int i = 0; i < 10000; i++) {
        spin_lock(&smp_test_lock);
        smp_test_counter++;
        spin_unlock(&smp_test_lock);
    }
}

uint64_t smp_selftest_finish(void)
{
    smp_selftest();
    while (atomic_load_u32((volatile uint32_t *)&secondary_test_done) == 0)
        cpu_relax();
    atomic_fence_rw();
    return smp_test_counter;
}

__attribute__((noreturn)) void secondary_main(uint64_t hartid)
{
    smp_init(hartid);
    vm_enable_hart();          /* satp is per-hart; switch this hart to Sv39 */
    trap_init_percpu();
    trap_enable_timer(10000);

    klog_puts("[qvos] secondary hart ");
    klog_hex(hartid);
    klog_puts(" online (tp=");
    klog_hex((uint64_t)hart_current()->kern_tf.gpr);
    klog_puts(")\n");

    atomic_store_u32_release((volatile uint32_t *)&secondary_ready, 1);
    smp_selftest();
    atomic_store_u32_release((volatile uint32_t *)&secondary_test_done, 1);

    scheduler();               /* join the global runqueue; never returns */
}
