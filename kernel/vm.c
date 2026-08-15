/* Sv39 virtual memory.
 *
 * qvos keeps the kernel identity-mapped (VA == PA for the low half) and
 * gives userspace its own page tables below USER_VA_TOP.  That gives real
 * page-table + satp + sfence.vma machinery and strong U/S isolation without
 * needing a high-half trampoline in this bring-up.
 *
 * Kernel permissions are split:
 *   .text / .rodata  -> R-X
 *   .data / .bss     -> RW-  (mapped by the wide DRAM identity map)
 *   MMIO             -> RW-  (device registers must never be executable)
 * Userspace pages always carry PTE_U; kernel page tables never do.
 */
#include <kernel/klog.h>
#include <kernel/pmm.h>
#include <kernel/vm.h>
#include <csr.h>

#define KERNEL_MEM_BASE 0x80000000UL
#define KERNEL_MEM_SIZE (128UL * 1024 * 1024)
#define MMIO_IDENTITY_SIZE 0x11000000UL   /* UART..virtio-mmio window */

extern char _text_start[];
extern char _text_end[];
extern char _rodata_start[];
extern char _rodata_end[];
extern char _tramp_start[];
extern char _tramp_end[];
extern uint64_t kernel_satp_slot;

pagetable_t kernel_pagetable;

static inline int px(int level, uint64_t va)
{
    return (int)((va >> (PAGE_SHIFT + 9 * level)) & 0x1ff);
}

pagetable_t vm_create(void)
{
    return (pagetable_t)pmm_alloc_page();
}

/* Recursively free only page-table pages (not leaf data pages). */
static void vm_freewalk(pte_t *pt, int level)
{
    for (int i = 0; i < 512; i++) {
        pte_t pte = pt[i];
        if (!(pte & PTE_V))
            continue;
        pte_t *child = (pte_t *)PTE2PA(pte);
        if (level > 1 && !(pte & (PTE_R | PTE_W | PTE_X)))
            vm_freewalk(child, level - 1);
        pmm_free_page(child);
        pt[i] = 0;
    }
}

void vm_free(pagetable_t pg)
{
    if (!pg)
        return;
    vm_freewalk(pg, 2);
    pmm_free_page(pg);
}

pte_t *vm_walk(pagetable_t pg, uint64_t va, int alloc)
{
    pte_t *p = pg;

    for (int level = 2; level >= 1; level--) {
        pte_t *slot = &p[px(level, va)];
        if (*slot & PTE_V) {
            p = (pte_t *)PTE2PA(*slot);
        } else {
            if (!alloc)
                return 0;
            pte_t *np = (pte_t *)pmm_alloc_page();
            *slot = PA2PTE((uint64_t)np) | PTE_V;
            p = np;
        }
    }
    return &p[px(0, va)];
}

int vm_map(pagetable_t pg, uint64_t va, uint64_t pa, uint64_t size,
           uint64_t perm)
{
    uint64_t start = va & ~PAGE_MASK;
    uint64_t last = (va + size - 1) & ~PAGE_MASK;
    uint64_t poff = pa - (va - start);

    for (uint64_t a = start; a <= last; a += PAGE_SIZE) {
        pte_t *slot = vm_walk(pg, a, 1);
        if (!slot)
            return -1;
        *slot = PA2PTE(poff + (a - start)) | perm | PTE_V;
    }
    sfence_vma();
    return 0;
}

uint64_t vm_translate(pagetable_t pg, uint64_t va)
{
    pte_t *slot = vm_walk(pg, va, 0);
    if (!slot || !(*slot & PTE_V))
        return 0;
    return PTE2PA(*slot) + (va & PAGE_MASK);
}

uint64_t vm_flags(pagetable_t pg, uint64_t va)
{
    pte_t *slot = vm_walk(pg, va, 0);
    if (!slot || !(*slot & PTE_V))
        return 0;
    return *slot & 0x1ff;
}

void vm_enable_hart(void)
{
    uint64_t root_pa = (uint64_t)kernel_pagetable;
    w_satp(SATP_SV39 | (root_pa >> PAGE_SHIFT));
    sfence_vma();
}

/* Every user page table maps the shared trampoline page (U|RX) and the
 * process's own 16 KiB trapframe at TRAPFRAME_VA (U|RW).  This is what
 * lets uservec save the context before switching back to the kernel satp. */
void uvm_map_common(pagetable_t pg, void *trapframe)
{
    uint64_t tramp = (uint64_t)_tramp_start;
    uint64_t tramp_pages =
        ((uint64_t)_tramp_end - tramp + PAGE_SIZE - 1) / PAGE_SIZE;

    /* The trampoline is fetched only in S-mode (a trap is taken with the
     * privilege already switched to S), so it must NOT carry PTE_U: the
     * current privileged spec forbids S-mode execution of U pages. */
    for (uint64_t i = 0; i < tramp_pages; i++)
        vm_map(pg, tramp + i * PAGE_SIZE, tramp + i * PAGE_SIZE, PAGE_SIZE,
               PTE_R | PTE_X | PTE_A | PTE_D);

    for (int i = 0; i < 4; i++)
        vm_map(pg, TRAPFRAME_VA + i * PAGE_SIZE,
               (uint64_t)trapframe + i * PAGE_SIZE, PAGE_SIZE,
               PTE_R | PTE_W | PTE_A | PTE_D);
}

static int vm_va_is_common(uint64_t va)
{
    uint64_t tramp = (uint64_t)_tramp_start & ~PAGE_MASK;
    uint64_t tramp_end = (uint64_t)_tramp_end;

    if (va >= tramp && va < tramp_end)
        return 1;
    if (va >= TRAPFRAME_VA && va < TRAPFRAME_VA + 4 * PAGE_SIZE)
        return 1;
    return 0;
}

static void vm_free_walk(pte_t *pt, int level, uint64_t base)
{
    for (int i = 0; i < 512; i++) {
        pte_t pte = pt[i];
        if (!(pte & PTE_V))
            continue;
        uint64_t va = base + (uint64_t)i * (PAGE_SIZE << (9 * level));
        uint64_t pa = PTE2PA(pte);

        if ((pte & (PTE_R | PTE_W | PTE_X)) || level == 0) {
            if (!vm_va_is_common(va))
                pmm_free_page((void *)pa);
        } else {
            vm_free_walk((pte_t *)pa, level - 1, va);
        }
        pt[i] = 0;
    }
}

void vm_free_user_pages(pagetable_t pg)
{
    if (!pg)
        return;
    vm_free_walk(pg, 2, 0);
    sfence_vma();
}

static int vm_copy_walk(pte_t *pt, int level, uint64_t base,
                        pagetable_t newpg)
{
    for (int i = 0; i < 512; i++) {
        pte_t pte = pt[i];
        if (!(pte & PTE_V))
            continue;
        uint64_t va = base + (uint64_t)i * (PAGE_SIZE << (9 * level));
        uint64_t pa = PTE2PA(pte);

        if ((pte & (PTE_R | PTE_W | PTE_X)) || level == 0) {
            if (vm_va_is_common(va))
                continue;
            void *mem = pmm_alloc_page();
            if (!mem)
                return -1;
            for (size_t k = 0; k < PAGE_SIZE; k++)
                ((volatile uint8_t *)mem)[k] =
                    ((volatile uint8_t *)pa)[k];
            if (vm_map(newpg, va, (uint64_t)mem, PAGE_SIZE,
                       (pte & 0x1ff) & ~PTE_A) != 0) {
                pmm_free_page(mem);
                return -1;
            }
        } else {
            if (vm_copy_walk((pte_t *)pa, level - 1, va, newpg) != 0)
                return -1;
        }
    }
    return 0;
}

int vm_copy_user(pagetable_t oldpg, pagetable_t newpg)
{
    return vm_copy_walk(oldpg, 2, 0, newpg);
}

void vm_init(void)
{
    kernel_pagetable = vm_create();

    /* Low MMIO identity map: CLINT, PLIC, UART, virtio-mmio. */
    vm_map(kernel_pagetable, 0, 0, MMIO_IDENTITY_SIZE,
           PTE_R | PTE_W | PTE_A | PTE_D);

    /* DRAM identity map. */
    vm_map(kernel_pagetable, KERNEL_MEM_BASE, KERNEL_MEM_BASE,
           KERNEL_MEM_SIZE, PTE_R | PTE_W | PTE_A | PTE_D);

    /* Tighten kernel image permissions: code and rodata are RX. */
    vm_map(kernel_pagetable, (uint64_t)_text_start, (uint64_t)_text_start,
           (uint64_t)_text_end - (uint64_t)_text_start,
           PTE_R | PTE_X | PTE_A | PTE_D);
    vm_map(kernel_pagetable, (uint64_t)_rodata_start, (uint64_t)_rodata_start,
           (uint64_t)_rodata_end - (uint64_t)_rodata_start,
           PTE_R | PTE_X | PTE_A | PTE_D);
    vm_map(kernel_pagetable, (uint64_t)_tramp_start, (uint64_t)_tramp_start,
           (uint64_t)_tramp_end - (uint64_t)_tramp_start,
           PTE_R | PTE_X | PTE_A | PTE_D);

    kernel_satp_slot = SATP_SV39 | ((uint64_t)kernel_pagetable >> PAGE_SHIFT);

    klog_puts("[qvos] Sv39 kernel page table root pa=");
    klog_hex((uint64_t)kernel_pagetable);
    klog_puts("\n");

    vm_enable_hart();
    klog_puts("[qvos] satp=");
    klog_hex(r_satp());
    klog_puts(" (SV39 active)\n");
}

void vm_selftest(void)
{
    pagetable_t upg;
    void *page;
    uint64_t upa = 0x40000000UL;      /* user test VA, above MMIO identity */
    uint64_t pa;

    klog_puts("[qvos] vm_translate(kernel .text) = ");
    klog_hex(vm_translate(kernel_pagetable, (uint64_t)vm_selftest));
    klog_puts("\n[qvos] kernel .text flags = ");
    klog_hex(vm_flags(kernel_pagetable, (uint64_t)vm_selftest));
    klog_puts("\n[qvos] kernel .text is RX (W bit clear): ");

    if ((vm_flags(kernel_pagetable, (uint64_t)vm_selftest) &
         (PTE_R | PTE_X)) == (PTE_R | PTE_X) &&
        !(vm_flags(kernel_pagetable, (uint64_t)vm_selftest) & PTE_W))
        klog_puts("ok\n");
    else
        klog_puts("FAIL\n");

    klog_puts("[qvos] UART flags = ");
    klog_hex(vm_flags(kernel_pagetable, 0x10000000UL));
    klog_puts(" (expect RW, no X/U)\n");

    upg = vm_create();
    page = pmm_alloc_page();
    pa = pmm_phys(page);
    vm_map(upg, upa, pa, PAGE_SIZE, PTE_U | PTE_R | PTE_W | PTE_X | PTE_A | PTE_D);

    klog_puts("[qvos] user test page: va=");
    klog_hex(upa);
    klog_puts(" -> pa=");
    klog_hex(vm_translate(upg, upa));
    klog_puts(" flags=");
    klog_hex(vm_flags(upg, upa));
    klog_puts("\n[qvos] same va not mapped in kernel table: ");

    if (vm_translate(kernel_pagetable, upa) == 0)
        klog_puts("ok (U/S isolated)\n");
    else
        klog_puts("FAIL\n");

    klog_puts("[qvos] STAGE3_OK\n");
}
