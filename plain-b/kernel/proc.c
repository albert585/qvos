/* Processes, per-process kernel stacks, fork, sleep/wakeup and the global
 * round-robin runqueue.
 *
 * Context-switch locking model (single global sched_lock):
 *   - scheduler() acquires sched_lock, picks p, swtch -> p.ctx.
 *   - a fresh p runs forkret() which drops sched_lock, then sret to U-mode.
 *   - when p later calls proc_yield/proc_sleep/proc_exit it acquires
 *     sched_lock and swtch -> hart.sched_ctx.
 *   - scheduler() resumes holding the lock and releases it; when p is
 *     scheduled again, p resumes after its swtch and releases the lock.
 * Every context that resumes from a swtch therefore owns sched_lock exactly
 * once, and every code path through a swtch releases exactly once.
 */
#include <kernel/kalloc.h>
#include <kernel/klog.h>
#include <kernel/file.h>
#include <kernel/pmm.h>
#include <kernel/proc.h>
#include <kernel/smp.h>
#include <kernel/spinlock.h>
#include <kernel/string.h>
#include <kernel/vm.h>
#include <csr.h>

struct proc proc_table[NPROC];
void *nanosleep_chan;          /* coarse sleep channel, woken by timer_tick */
static spinlock_t sched_lock;
static struct proc *runq_head;
static struct proc *runq_tail;
static int next_pid = 1;

static uint64_t align_up(uint64_t v, uint64_t a) { return (v + a - 1) & ~(a - 1); }

static void rq_push(struct proc *p)
{
    p->state = PROC_RUNNABLE;
    p->next = 0;
    if (runq_tail) {
        runq_tail->next = p;
    } else {
        runq_head = p;
    }
    runq_tail = p;
}

static struct proc *rq_pop(void)
{
    struct proc *p = runq_head;
    if (!p)
        return 0;
    runq_head = p->next;
    if (!runq_head)
        runq_tail = 0;
    p->next = 0;
    return p;
}

struct proc *myproc(void)
{
    struct proc *p = hart_current()->proc;
    if (!p)
        return 0;
    return p;
}

void forkret(void)
{
    /* scheduler() held sched_lock while swtching to this fresh context. */
    spin_unlock(&sched_lock);
    trap_return(myproc()->tf);
    __builtin_unreachable();
}

void scheduler(void)
{
    struct hart *h = hart_current();

    for (;;) {
        uint64_t flags = spin_lock_irqsave(&sched_lock);
        struct proc *p = rq_pop();

        if (!p) {
            spin_unlock_irqrestore(&sched_lock, flags);
            __asm__ volatile("wfi");
            continue;
        }

        p->state = PROC_RUNNING;
        p->quantum = TICK_QUANTUM;
        p->need_resched = 0;
        h->proc = p;

        swtch(&h->sched_ctx, &p->ctx);

        /* We resume here when p called proc_yield/proc_sleep/proc_exit and
         * switched back to us; we hold sched_lock again. */
        h->proc = 0;
        spin_unlock_irqrestore(&sched_lock, flags);
    }
}

int allocproc(void)
{
    struct proc *p = 0;

    for (int i = 0; i < NPROC; i++) {
        if (proc_table[i].state == PROC_UNUSED) {
            p = &proc_table[i];
            break;
        }
    }
    if (!p) {
        klog_puts("[dbg] allocproc: no slot\n");
        return -1;
    }

    p->tf = pmm_alloc_pages(4);      /* 16 KiB, page-aligned trapframe */
    if (!p->tf) {
        klog_puts("[dbg] allocproc: tf alloc failed\n");
        return -1;
    }
    p->kstack = pmm_alloc_pages(KSTACK_PAGES);
    if (!p->kstack) {
        klog_puts("[dbg] allocproc: kstack alloc failed\n");
        kfree(p->tf);
        p->tf = 0;
        return -1;
    }

    p->pid = next_pid++;
    p->state = PROC_EMBRYO;
    p->fresh = 1;
    p->parent = 0;
    p->chan = 0;
    p->killed = 0;
    p->exit_code = 0;
    p->quantum = TICK_QUANTUM;
    p->need_resched = 0;
    p->pg = 0;
    p->sz = 0;

    uint64_t top = (uint64_t)p->kstack + KSTACK_SIZE;
    p->tf->kernel_sp = top;
    p->ctx.ra = (uint64_t)forkret;
    p->ctx.sp = top - 8;
    for (int i = 0; i < 14; i++)
        ((uint64_t *)&p->ctx.s0)[i] = 0;
    for (int i = 0; i < NOFILE; i++)
        p->ofile[i] = 0;
    p->cwd[0] = '/';
    p->cwd[1] = 0;
    p->ofile[0] = file_console(0);
    p->ofile[1] = file_console(1);
    p->ofile[2] = file_console(2);

    return p->pid;
}

void freeproc(struct proc *p)
{
    for (int i = 0; i < NOFILE; i++) {
        if (p->ofile[i]) {
            file_close((struct file *)p->ofile[i]);
            p->ofile[i] = 0;
        }
    }
    if (p->pg) {
        vm_free_user_pages(p->pg);
        vm_free(p->pg);
        p->pg = 0;
    }
    if (p->tf) {
        pmm_free_pages(p->tf, 4);
        p->tf = 0;
    }
    if (p->kstack) {
        pmm_free_pages(p->kstack, KSTACK_PAGES);
        p->kstack = 0;
    }
    p->state = PROC_UNUSED;
    p->pid = 0;
    p->parent = 0;
}

int uvmalloc(pagetable_t pg, uint64_t oldsz, uint64_t newsz, uint64_t perm)
{
    if (newsz < oldsz)
        return (int)oldsz;
    oldsz = align_up(oldsz, PAGE_SIZE);
    for (uint64_t a = oldsz; a < newsz; a += PAGE_SIZE) {
        void *mem = pmm_alloc_page();
        if (!mem || vm_map(pg, a, (uint64_t)mem, PAGE_SIZE, perm) != 0) {
            uvmdealloc(pg, a, oldsz);
            return -1;
        }
    }
    return (int)newsz;
}

int uvmdealloc(pagetable_t pg, uint64_t oldsz, uint64_t newsz)
{
    if (newsz >= oldsz)
        return (int)oldsz;

    for (uint64_t a = align_up(newsz, PAGE_SIZE); a < oldsz; a += PAGE_SIZE) {
        pte_t *slot = vm_walk(pg, a, 0);
        if (slot && (*slot & PTE_V)) {
            uint64_t pa = PTE2PA(*slot);
            pmm_free_page((void *)pa);
            *slot = 0;
        }
    }
    sfence_vma();
    return (int)newsz;
}

void uvmfree(pagetable_t pg, uint64_t sz)
{
    for (uint64_t a = 0; a < sz; a += PAGE_SIZE) {
        pte_t *slot = vm_walk(pg, a, 0);
        if (slot && (*slot & PTE_V)) {
            uint64_t pa = PTE2PA(*slot);
            pmm_free_page((void *)pa);
            *slot = 0;
        }
    }
    sfence_vma();
}

int uvmcopy(pagetable_t oldpg, pagetable_t newpg, uint64_t sz)
{
    for (uint64_t a = 0; a < sz; a += PAGE_SIZE) {
        pte_t *slot = vm_walk(oldpg, a, 0);
        if (!slot || !(*slot & PTE_V))
            continue;
        uint64_t pa = PTE2PA(*slot);
        void *mem = pmm_alloc_page();
        if (!mem)
            return -1;
        for (size_t i = 0; i < PAGE_SIZE; i++)
            ((volatile uint8_t *)mem)[i] = ((volatile uint8_t *)pa)[i];
        uint64_t perm = (*slot & 0x1ff) & ~PTE_A;   /* keep U/R/W/X */
        if (vm_map(newpg, a, (uint64_t)mem, PAGE_SIZE, perm) != 0) {
            pmm_free_page(mem);
            return -1;
        }
    }
    sfence_vma();
    return 0;
}

int copyin(pagetable_t pg, void *dst, uint64_t srcva, size_t len)
{
    uint8_t *d = (uint8_t *)dst;

    while (len > 0) {
        uint64_t pa = vm_translate(pg, srcva);
        if (!pa)
            return -1;
        size_t n = PAGE_SIZE - (srcva & PAGE_MASK);
        if (n > len)
            n = len;
        for (size_t i = 0; i < n; i++)
            d[i] = ((volatile uint8_t *)pa)[i];
        d += n;
        srcva += n;
        len -= n;
    }
    return 0;
}

int copyinstr(pagetable_t pg, char *dst, uint64_t srcva, size_t max)
{
    size_t i = 0;
    while (i < max) {
        uint64_t pa = vm_translate(pg, srcva + i);
        if (!pa)
            return -1;
        dst[i] = ((volatile uint8_t *)pa)[0];
        if (dst[i] == 0)
            return 0;
        i++;
    }
    return -1;
}

void proc_yield(void)
{
    struct proc *p = myproc();
    struct hart *h = hart_current();
    if (!p || p->state != PROC_RUNNING)
        return;

    uint64_t flags = spin_lock_irqsave(&sched_lock);
    if (p->state == PROC_RUNNING) {
        p->state = PROC_RUNNABLE;
        p->need_resched = 0;
        rq_push(p);
        swtch(&p->ctx, &h->sched_ctx);
    }
    spin_unlock_irqrestore(&sched_lock, flags);
}

void proc_sleep(void *chan)
{
    struct proc *p = myproc();
    struct hart *h = hart_current();
    if (!p || chan == 0)
        return;

    uint64_t flags = spin_lock_irqsave(&sched_lock);
    if (p->state == PROC_RUNNING) {
        p->state = PROC_SLEEPING;
        p->chan = chan;
        swtch(&p->ctx, &h->sched_ctx);
        p->chan = 0;
    }
    spin_unlock_irqrestore(&sched_lock, flags);
}

void proc_wakeup(void *chan)
{
    uint64_t flags = spin_lock_irqsave(&sched_lock);
    for (int i = 0; i < NPROC; i++) {
        struct proc *p = &proc_table[i];
        if (p->state == PROC_SLEEPING && p->chan == chan) {
            p->state = PROC_RUNNABLE;
            p->chan = 0;
            rq_push(p);
        }
    }
    spin_unlock_irqrestore(&sched_lock, flags);
}

void proc_tick(void)
{
    struct proc *p = myproc();
    if (!p || p->state != PROC_RUNNING)
        return;
    if (p->quantum > 0 && --p->quantum == 0)
        p->need_resched = 1;
}

void proc_exit(int status)
{
    struct proc *p = myproc();
    struct hart *h = hart_current();
    if (!p)
        return;

    uint64_t flags = spin_lock_irqsave(&sched_lock);
    p->exit_code = status;
    p->state = PROC_ZOMBIE;

    /* Wake a parent blocked in waitpid on itself. */
    if (p->parent && p->parent->state == PROC_SLEEPING &&
        p->parent->chan == p->parent) {
        p->parent->state = PROC_RUNNABLE;
        p->parent->chan = 0;
        rq_push(p->parent);
    }

    swtch(&p->ctx, &h->sched_ctx);
    spin_unlock_irqrestore(&sched_lock, flags);   /* unreachable */
    __builtin_unreachable();
}

int proc_fork(void)
{
    struct proc *parent = myproc();
    if (!parent)
        return -1;

    int pid = allocproc();
    if (pid < 0)
        return -1;
    struct proc *child = 0;
    for (int i = 0; i < NPROC; i++) {
        if (proc_table[i].state == PROC_EMBRYO &&
            proc_table[i].pid == pid) {
            child = &proc_table[i];
            break;
        }
    }
    if (!child)
        return -1;

    child->pg = vm_create();
    if (!child->pg || vm_copy_user(parent->pg, child->pg) != 0) {
        freeproc(child);
        return -1;
    }
    uvm_map_common(child->pg, child->tf);

    *child->tf = *parent->tf;
    child->tf->satp = SATP_SV39 | ((uint64_t)child->pg >> PAGE_SHIFT);
    child->tf->gpr[10] = 0;                       /* fork() == 0 in child */
    child->tf->kernel_sp = (uint64_t)child->kstack + KSTACK_SIZE;
    child->sz = parent->sz;
    child->ppid = parent->pid;
    child->parent = parent;
    child->fresh = 1;
    strncpy(child->cwd, parent->cwd, sizeof(child->cwd) - 1);
    child->cwd[sizeof(child->cwd) - 1] = 0;
    for (int i = 0; i < NOFILE; i++) {
        child->ofile[i] = parent->ofile[i];
        if (child->ofile[i])
            file_dup((struct file *)child->ofile[i]);
    }

    uint64_t flags = spin_lock_irqsave(&sched_lock);
    child->state = PROC_RUNNABLE;
    rq_push(child);
    spin_unlock_irqrestore(&sched_lock, flags);

    return pid;
}

int proc_waitpid(int pid, int *status, int options)
{
    (void)options;
    struct proc *p = myproc();

    for (;;) {
        int havekids = 0;
        for (int i = 0; i < NPROC; i++) {
            struct proc *q = &proc_table[i];
            if (q->parent != p)
                continue;
            havekids = 1;
            if (pid > 0 && q->pid != pid)
                continue;
            if (q->state == PROC_ZOMBIE) {
                int found = q->pid;
                if (status)
                    *status = q->exit_code;
                freeproc(q);
                return found;
            }
        }
        if (!havekids)
            return -1;
        proc_sleep(p);      /* child exit wakes us */
    }
}

/* Stage 5 test-user creation is not part of the plain-b standalone kernel.
 * The boot path uses the ELF loader (proc_exec) exclusively. */
int proc_create_test_user(void)
{
    (void)0;
    return -1;
}

/* Spawn /bin/init (or any FAT32 ELF) as the first user process. */
int proc_spawn(const char *path, char *const argv[])
{
    int pid = allocproc();
    if (pid < 0)
        return -1;

    struct proc *p = 0;
    for (int i = 0; i < NPROC; i++) {
        if (proc_table[i].state == PROC_EMBRYO && proc_table[i].pid == pid) {
            p = &proc_table[i];
            break;
        }
    }
    if (!p)
        return -1;

    p->pg = vm_create();
    if (!p->pg) {
        freeproc(p);
        return -1;
    }
    uvm_map_common(p->pg, p->tf);

    struct hart *h = hart_current();
    struct proc *old = h->proc;
    h->proc = p;
    int r = proc_exec(path, argv);
    h->proc = old;
    if (r != 0) {
        freeproc(p);
        return -1;
    }

    uint64_t flags = spin_lock_irqsave(&sched_lock);
    p->state = PROC_RUNNABLE;
    rq_push(p);
    spin_unlock_irqrestore(&sched_lock, flags);
    return pid;
}

void proc_kill_current(int cause)
{
    klog_puts("[qvos] user trap killed pid ");
    klog_hex((uint64_t)myproc()->pid);
    klog_puts(" cause ");
    klog_hex((uint64_t)cause);
    klog_puts("\n");
    proc_exit(-1);
}

void proc_init(void)
{
    for (int i = 0; i < NPROC; i++) {
        proc_table[i].state = PROC_UNUSED;
        proc_table[i].next = 0;
    }
    runq_head = runq_tail = 0;
}
