/* Unified S-mode trap dispatcher.
 *
 * Interrupt path:
 *   timer -> proc_tick/scheduler quantum; re-arm via stimecmp (Sstc)
 * Exceptions from U-mode:
 *   ecall -> syscall(); any other fault kills the process
 * Exceptions from S-mode:
 *   ecall -> boot self-test; anything else is an unrecoverable kernel trap
 */
#include <kernel/klog.h>
#include <kernel/panic.h>
#include <kernel/proc.h>
#include <kernel/sbi.h>
#include <kernel/smp.h>
#include <csr.h>
#include <trapframe.h>

extern void uservec(void);
extern void *nanosleep_chan;

static inline uint64_t rdtime(void)
{
    uint64_t t;
    __asm__ volatile("rdtime %0" : "=r"(t));
    return t;
}

/* QEMU virt timebase is 10 MHz. */
#define TIMER_FREQ_HZ 10000000UL

void trap_enable_timer(uint64_t interval_us)
{
    uint64_t next = rdtime() + (TIMER_FREQ_HZ / 1000000UL) * interval_us;
    w_stimecmp(next);   /* Sstc direct path (see Stage 1 notes) */
}

static void timer_tick(void)
{
    struct hart *h = hart_current();
    h->ticks++;
    trap_enable_timer(10000);          /* 10 ms scheduler tick */

    proc_tick();
    proc_wakeup(&nanosleep_chan);      /* coarse nanosleep wake */
}

__attribute__((noreturn)) static void kernel_panic_trap(uint64_t cause,
                                                        uint64_t stval,
                                                        uint64_t sepc)
{
    panic("unrecoverable kernel trap", cause, stval, sepc);
}

/* C entry for U-mode traps; uservec already saved the context into the
 * current process's trapframe. */
struct trapframe *trap_dispatch_user_c(void)
{
    struct proc *p = myproc();
    if (!p) {
        kernel_panic_trap(0xdead0001, 0, 0);
    }
    return trap_dispatch(p->tf);
}

struct trapframe *trap_dispatch(struct trapframe *tf)
{
    uint64_t cause = tf->scause;
    int from_user = !(tf->sstatus & SSTATUS_SPP);

    if (cause & SCAUSE_INTERRUPT) {
        switch (SCAUSE_CODE(cause)) {
        case 1:  /* supervisor software interrupt (SBI IPI) */
            csr_clear(sip, SIE_SSIE);
            break;
        case 5:  /* supervisor timer interrupt */
            timer_tick();
            break;
        case 9:  /* supervisor external interrupt (PLIC) */
            klog_puts("[qvos] SEIP interrupt (PLIC driver arrives in Stage 8)\n");
            break;
        default:
            kernel_panic_trap(cause, tf->stval, tf->sepc);
        }
    } else {
        switch (SCAUSE_CODE(cause)) {
        case 8:  /* ecall from U-mode */
            tf->sepc += 4;
            syscall(tf);
            break;
        case 9:  /* ecall from S-mode (boot self-test only) */
            tf->sepc += 4;
            break;
        default:
            if (from_user) {
                klog_puts("[qvos] user exception: pid=");
                if (myproc())
                    klog_hex((uint64_t)myproc()->pid);
                klog_puts(" cause=");
                klog_hex(cause);
                klog_puts(" stval=");
                klog_hex(tf->stval);
                klog_puts(" sepc=");
                klog_hex(tf->sepc);
                klog_puts("\n");
                proc_kill_current((int)cause);
            }
            kernel_panic_trap(cause, tf->stval, tf->sepc);
        }
    }

    /* Preempt a user process whose quantum expired. */
    if (from_user && myproc() && myproc()->need_resched) {
        myproc()->need_resched = 0;
        proc_yield();
    }

    if (from_user && myproc())
        return myproc()->tf;
    return tf;
}

void trap_init_percpu(void)
{
    uint64_t vlenb = r_vlenb();

    w_stvec((uint64_t)uservec);     /* trampoline distinguishes U/S traps */
    w_sscratch(0);                  /* kernel is running: sscratch must be 0 */

    if (vlenb == 0 || vlenb > QVOS_MAX_VLENB) {
        klog_puts("[qvos] unsupported VLENB = ");
        klog_hex(vlenb);
        klog_puts(" (max ");
        klog_hex(QVOS_MAX_VLENB);
        klog_puts(")\n");
        for (;;)
            __asm__ volatile("wfi");
    }

    w_sie(r_sie() | SIE_STIE);
    w_sstatus(r_sstatus() | SSTATUS_SIE);

    klog_puts("[qvos] traps armed: stvec=");
    klog_hex(r_stvec());
    klog_puts(" vlenb=");
    klog_hex(vlenb);
    klog_puts("\n");
}
