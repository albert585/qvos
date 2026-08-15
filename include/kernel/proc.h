#ifndef QVOS_PROC_H
#define QVOS_PROC_H

#include <stdint.h>
#include <trapframe.h>
#include <kernel/context.h>
#include <kernel/vm.h>

#define NPROC        64
#define KSTACK_PAGES 4
#define KSTACK_SIZE  (KSTACK_PAGES * PAGE_SIZE)
#define NOFILE       16
#define TICK_QUANTUM 10          /* scheduler ticks (10 ms each) */

enum procstate {
    PROC_UNUSED = 0,
    PROC_EMBRYO,
    PROC_RUNNABLE,
    PROC_RUNNING,
    PROC_SLEEPING,
    PROC_ZOMBIE,
};

struct proc {
    enum procstate state;
    int    pid;
    int    ppid;
    int    exit_code;
    int    quantum;
    int    need_resched;
    int    killed;
    int    fresh;                /* first run: forkret must drop sched_lock */

    pagetable_t pg;
    uint64_t sz;                 /* user image size in bytes */

    struct trapframe *tf;
    void   *kstack;              /* per-process kernel stack (KSTACK_SIZE) */
    struct context ctx;

    struct proc *parent;
    struct proc *next;         /* runqueue link */
    void  *chan;               /* sleep channel */
    char   name[16];

    void  *ofile[NOFILE];        /* struct file*, filled in Stage 6 */
};

extern struct proc proc_table[NPROC];

void  swtch(struct context *oldctx, struct context *newctx);
void  forkret(void);
void  syscall(struct trapframe *tf);

void  proc_init(void);
struct proc *myproc(void);
int   proc_fork(void);
int   proc_create_test_user(void);
int   proc_spawn(const char *path, char *const argv[]);
void  proc_exit(int status);
int   proc_waitpid(int pid, int *status, int options);
void  proc_yield(void);
void  proc_sleep(void *chan);
void  proc_wakeup(void *chan);
void  proc_tick(void);
void  proc_kill_current(int cause);
void  scheduler(void) __attribute__((noreturn));

int   allocproc(void);
void  freeproc(struct proc *p);
int   uvmalloc(pagetable_t pg, uint64_t oldsz, uint64_t newsz, uint64_t perm);
int   uvmdealloc(pagetable_t pg, uint64_t oldsz, uint64_t newsz);
void  uvmfree(pagetable_t pg, uint64_t sz);
int   uvmcopy(pagetable_t oldpg, pagetable_t newpg, uint64_t sz);

int   copyin(pagetable_t pg, void *dst, uint64_t srcva, size_t len);
int   copyinstr(pagetable_t pg, char *dst, uint64_t srcva, size_t max);
int   proc_exec(const char *path, char *const argv[]);

#endif
