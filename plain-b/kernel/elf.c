/* ELF64 RISC-V loader and execve implementation.
 *
 * exec creates a brand-new user page table, maps the trampoline + the
 * process's existing trapframe pages, loads every PT_LOAD segment with the
 * final PTE permissions (RX code / RW data / RW BSS), builds an initial
 * user stack with argc/argv, then atomically swaps the process to the new
 * image and frees the old address space.
 */
#include <kernel/elf.h>
#include <kernel/fat32.h>
#include <kernel/file.h>
#include <kernel/kalloc.h>
#include <kernel/klog.h>
#include <kernel/pmm.h>
#include <kernel/proc.h>
#include <kernel/string.h>
#include <kernel/vm.h>
#include <csr.h>

#define USER_STACK_PAGES 8

static uint64_t align_up(uint64_t v, uint64_t a) { return (v + a - 1) & ~(a - 1); }

uint64_t elf_load(pagetable_t pg, const uint8_t *image, size_t size,
                  uint64_t *brk)
{
    if (size < sizeof(elf64_ehdr_t))
        return 0;
    const elf64_ehdr_t *eh = (const elf64_ehdr_t *)image;

    if (*(const uint32_t *)eh->ident != ELF_MAGIC ||
        eh->ident[4] != ELFCLASS64 || eh->ident[5] != ELFDATA2LSB ||
        eh->machine != EM_RISCV || eh->type != ET_EXEC) {
        klog_puts("[qvos] exec: not an ELF64 RISC-V executable\n");
        return 0;
    }

    uint64_t brkval = 0;

    for (int i = 0; i < eh->phnum; i++) {
        const elf64_phdr_t *ph = (const elf64_phdr_t *)(image + eh->phoff +
                                                        i * eh->phentsize);
        if (ph->type != PT_LOAD)
            continue;
        if (ph->memsz == 0)
            continue;

        uint64_t va_start = ph->vaddr & ~PAGE_MASK;
        uint64_t va_end = align_up(ph->vaddr + ph->memsz, PAGE_SIZE);
        uint64_t final_perm = PTE_U | PTE_R | PTE_A | PTE_D;
        if (ph->flags & PF_W)
            final_perm |= PTE_W;
        if (ph->flags & PF_X)
            final_perm |= PTE_X;

        for (uint64_t va = va_start; va < va_end; va += PAGE_SIZE) {
            void *mem = pmm_alloc_page();
            if (!mem)
                return 0;
            if (vm_map(pg, va, (uint64_t)mem, PAGE_SIZE,
                       PTE_U | PTE_R | PTE_W | PTE_X | PTE_A | PTE_D) != 0)
                return 0;

            /* Copy the file-backed part of this page.  The segment may
             * start mid-page: data must land at (vaddr - page_start). */
            uint64_t page_off = (va < ph->vaddr) ? (ph->vaddr - va) : 0;
            uint64_t seg_off = (va + page_off) - ph->vaddr;
            if (seg_off < ph->filesz) {
                uint64_t file_off = ph->offset + seg_off;
                uint64_t n = PAGE_SIZE - page_off;
                if (n > ph->filesz - seg_off)
                    n = ph->filesz - seg_off;
                if (file_off + n > size)
                    return 0;
                memcpy((uint8_t *)mem + page_off, image + file_off, n);
            }
            /* Bytes past filesz were already zeroed by pmm_alloc_page. */

            if ((ph->flags & PF_W) == 0) {
                /* Tighten to RX (or R) after loading. */
                vm_map(pg, va, (uint64_t)mem, PAGE_SIZE, final_perm);
            }
        }

        if (va_end > brkval)
            brkval = va_end;
    }

    *brk = brkval;
    return eh->entry;
}

/* Copy one argv string into the new user stack; returns the VA written. */
static uint64_t stack_push_str(pagetable_t pg, uint64_t *sp,
                               const char *s, size_t len)
{
    *sp -= len + 1;
    *sp &= ~(uint64_t)0xf;
    uint64_t va = *sp;

    for (size_t i = 0; i < len; i++) {
        uint64_t pa = vm_translate(pg, va + i);
        if (!pa)
            return 0;
        ((volatile uint8_t *)pa)[0] = s[i];
    }
    uint64_t pa = vm_translate(pg, va + len);
    if (!pa)
        return 0;
    ((volatile uint8_t *)pa)[0] = 0;
    return va;
}

int proc_exec(const char *path, char *const argv[])
{
    struct proc *p = myproc();
    if (!p)
        return -1;

    int argc = 0;
    if (argv) {
        while (argv[argc])
            argc++;
    }

    /* Read the executable through the VFS so relative paths, mount points
     * and the FAT32 root all work. */
    struct file *fp = file_open(path, 0);   /* O_RDONLY */
    if (!fp || fp->type != FD_FAT || fp->fat.is_dir) {
        if (fp)
            file_close(fp);
        klog_puts("[qvos] exec: not found: ");
        klog_puts(path);
        klog_puts("\n");
        return -1;
    }
    uint32_t fsize = fp->fat.size;
    uint8_t *image = kmalloc(fsize ? fsize : 1);
    if (!image) {
        file_close(fp);
        return -1;
    }
    if (file_read(fp, image, fsize) != (int)fsize) {
        file_close(fp);
        kfree(image);
        return -1;
    }
    file_close(fp);

    pagetable_t newpg = vm_create();
    if (!newpg) {
        kfree(image);
        return -1;
    }
    uvm_map_common(newpg, p->tf);

    uint64_t brk = 0;
    uint64_t entry = elf_load(newpg, image, fsize, &brk);
    kfree(image);
    if (!entry) {
        vm_free_user_pages(newpg);
        vm_free(newpg);
        return -1;
    }

    /* Initial user stack. */
    uint64_t sp = USER_STACK_TOP;
    for (int i = 0; i < USER_STACK_PAGES; i++) {
        void *mem = pmm_alloc_page();
        if (!mem || vm_map(newpg, USER_STACK_TOP - USER_STACK_PAGES * PAGE_SIZE +
                           i * PAGE_SIZE, (uint64_t)mem, PAGE_SIZE,
                           PTE_U | PTE_R | PTE_W | PTE_A | PTE_D) != 0)
            goto fail;
    }

    uint64_t uargv[64];
    if (argc > 63)
        goto fail;
    for (int i = 0; i < argc; i++) {
        size_t len = strlen(argv[i]);
        uargv[i] = stack_push_str(newpg, &sp, argv[i], len);
        if (!uargv[i])
            goto fail;
    }

    sp &= ~(uint64_t)0xf;
    sp -= 8;
    *(volatile uint64_t *)vm_translate(newpg, sp) = 0;   /* envp terminator */
    sp -= (uint64_t)(argc + 1) * 8;
    for (int i = 0; i < argc; i++)
        *(volatile uint64_t *)vm_translate(newpg, sp + i * 8) = uargv[i];
    *(volatile uint64_t *)vm_translate(newpg, sp + argc * 8) = 0;
    uint64_t argv_va = sp;
    sp -= 8;
    *(volatile uint64_t *)vm_translate(newpg, sp) = (uint64_t)argc;

    /* Atomic swap to the new image. */
    pagetable_t oldpg = p->pg;
    p->pg = newpg;
    p->sz = brk;
    p->tf->sepc = entry;
    p->tf->sstatus = SSTATUS_SPIE | SSTATUS_FS_INITIAL | SSTATUS_VS_INITIAL;
    p->tf->satp = SATP_SV39 | ((uint64_t)newpg >> PAGE_SHIFT);
    p->tf->gpr[2] = sp;
    p->tf->gpr[10] = (uint64_t)argc;
    p->tf->gpr[11] = argv_va;
    p->tf->gpr[0] = 0;

    if (oldpg) {
        vm_free_user_pages(oldpg);
        vm_free(oldpg);
    }
    return 0;

fail:
    vm_free_user_pages(newpg);
    vm_free(newpg);
    return -1;
}
