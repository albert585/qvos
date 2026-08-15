/* Physical page allocator: singly-linked free list over the 4 KiB frames of
 * the 128 MiB QEMU virt DRAM.  Frames below _kernel_end (kernel image, bss,
 * per-hart trapframes, the 1 MiB .early area) and the DTB trailer are
 * excluded.  The free-list links live inside the free pages themselves, so
 * the allocator needs no metadata array.
 */
#include <kernel/klog.h>
#include <kernel/pmm.h>
#include <kernel/spinlock.h>
#include <kernel/vm.h>

#define RAM_BASE 0x80000000UL
#define RAM_END  (0x80000000UL + 128UL * 1024 * 1024)

extern char _kernel_end[];

struct pframe {
    struct pframe *next;
};

static struct pframe *free_list;
static uint64_t total_free;
static uint64_t total_frames;
static spinlock_t pmm_lock;

static uint64_t align_up(uint64_t v, uint64_t a) { return (v + a - 1) & ~(a - 1); }
static uint64_t align_down(uint64_t v, uint64_t a) { return v & ~(a - 1); }

static void frame_zero(void *p)
{
    volatile uint64_t *q = (volatile uint64_t *)p;
    for (unsigned long i = 0; i < PAGE_SIZE / 8; i++)
        q[i] = 0;
}

void pmm_init(uint64_t dtb_pa)
{
    uint64_t start = align_up((uint64_t)_kernel_end, PAGE_SIZE);
    uint64_t end = RAM_END;

    /* The DTB lives just below RAM_END on QEMU virt; keep it safe. */
    if (dtb_pa >= RAM_BASE && dtb_pa < RAM_END)
        end = align_down(dtb_pa, PAGE_SIZE);
    if (start >= end) {
        klog_puts("[qvos] PANIC: no free DRAM for pmm\n");
        for (;;)
            __asm__ volatile("wfi");
    }

    free_list = 0;
    for (uint64_t p = end - PAGE_SIZE; p >= start; p -= PAGE_SIZE) {
        struct pframe *f = (struct pframe *)p;
        f->next = free_list;
        free_list = f;
        total_free++;
    }
    total_frames = total_free;

    klog_puts("[qvos] pmm free DRAM: ");
    klog_hex(start);
    klog_puts(" .. ");
    klog_hex(end);
    klog_puts("\n");
}

void *pmm_alloc_page(void)
{
    uint64_t flags = spin_lock_irqsave(&pmm_lock);
    struct pframe *f = free_list;
    if (!f) {
        spin_unlock_irqrestore(&pmm_lock, flags);
        klog_puts("[qvos] PANIC: out of physical memory\n");
        for (;;)
            __asm__ volatile("wfi");
    }
    free_list = f->next;
    total_free--;
    spin_unlock_irqrestore(&pmm_lock, flags);

    frame_zero(f);
    return f;
}

void pmm_free_page(void *page)
{
    if (!page)
        return;
    uint64_t flags = spin_lock_irqsave(&pmm_lock);
    struct pframe *f = (struct pframe *)page;
    f->next = free_list;
    free_list = f;
    total_free++;
    spin_unlock_irqrestore(&pmm_lock, flags);
}

void *pmm_alloc_pages(size_t n)
{
    if (n == 0)
        return 0;

    uint64_t flags = spin_lock_irqsave(&pmm_lock);

    /* Find a run of n consecutive frames in the free list and detach it. */
    struct pframe **link = &free_list;
    struct pframe **run_link = 0;
    struct pframe *prev = 0;
    struct pframe *cur = 0;
    size_t run = 0;

    while (*link) {
        cur = *link;
        if (prev && (uint64_t)prev + PAGE_SIZE != (uint64_t)cur) {
            run = 0;
            run_link = 0;
        }
        if (run == 0)
            run_link = link;
        run++;
        if (run == n)
            break;
        prev = cur;
        link = &cur->next;
    }

    if (run != n) {
        spin_unlock_irqrestore(&pmm_lock, flags);
        return 0;
    }

    struct pframe *first = *run_link;
    *run_link = cur->next;
    total_free -= n;
    spin_unlock_irqrestore(&pmm_lock, flags);

    for (size_t i = 0; i < n; i++)
        frame_zero((char *)first + i * PAGE_SIZE);
    return first;
}

void pmm_free_pages(void *first, size_t n)
{
    for (size_t i = 0; i < n; i++)
        pmm_free_page((char *)first + i * PAGE_SIZE);
}

uint64_t pmm_phys(void *ptr)
{
    return (uint64_t)ptr;   /* kernel is identity-mapped */
}

void pmm_report(void)
{
    klog_puts("[qvos] pmm: ");
    klog_hex(total_free);
    klog_puts(" free frames of ");
    klog_hex(total_frames);
    klog_puts("\n");
}
