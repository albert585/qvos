/* General-purpose kernel heap: first-fit free list with immediate
 * coalescing and page-granular expansion from pmm.  Block headers live
 * immediately before the returned pointer; both header and payload are
 * 16-byte aligned.
 */
#include <kernel/kalloc.h>
#include <kernel/klog.h>
#include <kernel/pmm.h>
#include <kernel/spinlock.h>
#include <kernel/vm.h>

#define KH_MAGIC_FREE 0x4b46524545000000UL   /* "KFREE"  */
#define KH_MAGIC_USED 0x4b55534544000000UL   /* "KUSED"  */

struct kblk {
    uint64_t magic;
    uint64_t size;      /* total block size including this header */
    struct kblk *next;  /* free-list link; scratch while allocated */
};

static struct kblk *free_head;          /* sorted by address */
static spinlock_t    kalloc_lock;

static uint64_t align_up(uint64_t v, uint64_t a) { return (v + a - 1) & ~(a - 1); }

static void klist_insert(struct kblk *b)
{
    b->next = 0;
    struct kblk **pp = &free_head;
    while (*pp && (uint64_t)(*pp) < (uint64_t)b)
        pp = &(*pp)->next;
    b->next = *pp;
    *pp = b;
}

static void klist_unlink(struct kblk *b)
{
    struct kblk **pp = &free_head;
    while (*pp && *pp != b)
        pp = &(*pp)->next;
    if (*pp == b)
        *pp = b->next;
}

static void kcoalesce(void)
{
    struct kblk *b = free_head;

    while (b && b->next) {
        struct kblk *n = b->next;
        if ((uint64_t)b + b->size == (uint64_t)n) {
            b->size += n->size;
            b->next = n->next;
            continue;
        }
        b = n;
    }
}

static int kexpand(size_t want)
{
    size_t pages = (want + PAGE_SIZE - 1) / PAGE_SIZE;
    if (pages < 4)
        pages = 4;                       /* amortize heap growth */
    void *mem = pmm_alloc_pages(pages);
    if (!mem)
        return -1;

    struct kblk *b = (struct kblk *)mem;
    b->magic = KH_MAGIC_FREE;
    b->size = pages * PAGE_SIZE;
    b->next = 0;
    klist_insert(b);
    kcoalesce();
    return 0;
}

void *kmalloc(size_t size)
{
    if (size == 0)
        return 0;

    uint64_t flags = spin_lock_irqsave(&kalloc_lock);
    uint64_t need = align_up(size + sizeof(struct kblk), 16);

    struct kblk *b = free_head;
    while (b && b->size < need)
        b = b->next;

    if (!b) {
        if (kexpand(need) != 0) {
            spin_unlock_irqrestore(&kalloc_lock, flags);
            return 0;
        }
        b = free_head;
        while (b && b->size < need)
            b = b->next;
        if (!b) {
            spin_unlock_irqrestore(&kalloc_lock, flags);
            return 0;
        }
    }

    klist_unlink(b);
    if (b->size >= need + sizeof(struct kblk) + 16) {
        struct kblk *rest = (struct kblk *)((uint64_t)b + need);
        rest->magic = KH_MAGIC_FREE;
        rest->size = b->size - need;
        rest->next = 0;
        klist_insert(rest);
        b->size = need;
    }

    b->magic = KH_MAGIC_USED;
    b->next = 0;
    spin_unlock_irqrestore(&kalloc_lock, flags);

    return (void *)(b + 1);
}

void *kzalloc(size_t size)
{
    void *p = kmalloc(size);
    if (!p)
        return 0;
    volatile uint8_t *q = (volatile uint8_t *)p;
    for (size_t i = 0; i < size; i++)
        q[i] = 0;
    return p;
}

void kfree(void *ptr)
{
    if (!ptr)
        return;

    struct kblk *b = (struct kblk *)ptr - 1;
    if (b->magic != KH_MAGIC_USED) {
        klog_puts("[qvos] kfree: bad block magic ");
        klog_hex((uint64_t)ptr);
        klog_puts("\n");
        return;
    }

    uint64_t flags = spin_lock_irqsave(&kalloc_lock);
    b->magic = KH_MAGIC_FREE;
    b->next = 0;
    klist_insert(b);
    kcoalesce();
    spin_unlock_irqrestore(&kalloc_lock, flags);
}

void *kalloc_page(void)
{
    return pmm_alloc_page();
}

void kfree_page(void *page)
{
    pmm_free_page(page);
}

void kalloc_selftest(void)
{
    char *a = kmalloc(37);
    char *b = kmalloc(100);
    char *c = kmalloc(4096 + 13);
    char *d = kmalloc(16);

    if (!a || !b || !c || !d) {
        klog_puts("[qvos] kalloc selftest FAIL (allocation returned NULL)\n");
        return;
    }

    for (int i = 0; i < 37; i++) a[i] = (char)(i ^ 0x5a);
    for (int i = 0; i < 100; i++) b[i] = (char)i;
    c[4096] = 0x7f;
    d[0] = 'q';

    int ok = 1;
    for (int i = 0; i < 37; i++)
        if (a[i] != (char)(i ^ 0x5a)) ok = 0;
    for (int i = 0; i < 100; i++)
        if (b[i] != (char)i) ok = 0;
    if (c[4096] != 0x7f || d[0] != 'q') ok = 0;

    klog_puts("[qvos] kalloc blocks: ");
    klog_hex((uint64_t)a);
    klog_puts(" ");
    klog_hex((uint64_t)b);
    klog_puts(" ");
    klog_hex((uint64_t)c);
    klog_puts(" ");
    klog_hex((uint64_t)d);
    klog_puts("\n");

    kfree(a);
    kfree(c);
    kfree(b);
    kfree(d);

    /* Realloc after coalescing must still work. */
    char *e = kmalloc(64);
    if (!e || (uint64_t)e >= (uint64_t)d + 4096) ok = 0;
    kfree(e);

    klog_puts(ok ? "[qvos] kalloc selftest ok\n"
                 : "[qvos] kalloc selftest FAIL\n");
}
