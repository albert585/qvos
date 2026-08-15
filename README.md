# qvos
> This is an AI project.
A small S-mode kernel + POSIX-subset userspace for a dual-core RV64IMAFCV
platform, brought up and regression-tested on QEMU `riscv64 virt`.

## What is implemented
- OpenSBI `fw_dynamic` → kernel entry in S-mode (boot hart = whichever hart OpenSBI chooses)
- Unified S-mode trap vector with Sv39 trampoline (S-only trampoline page)
- Full context save/restore: GPRs + FPRs + `v0–v31` (whole-register, VLENB bytes each)
- Timer via Sstc `stimecmp` (SBI TIME kept as fallback)
- Dual-hart SMP: HSM hart start, per-hart data, AMO spinlocks, global RR scheduler
- Sv39 paging: kernel RX/RW split, user U/RWX pages, `sfence.vma`, user/kernel isolation
- DRAM free-list page allocator + first-fit `kmalloc/kfree`
- Processes: fork/exec/wait/exit/yield/sleep, pipe, per-process kernel stacks and `swtch`
- FAT32 RAM virtual disk (16 MiB, mtools-built), BPB/FAT/cluster-chain/directory parser
- VFS-lite: console files, FAT files, pipes; open/read/write/close/lseek/fstat/dup
- ELF64 RISC-V loader with correct segment permissions and initial argv stack
- Userspace CRT + libc subset: printf/malloc/string/syscall wrappers
- `/bin/init` → `/bin/ostest` (all PASS) → `/bin/sh` small shell
- virtio-mmio GPU: 32bpp framebuffer, `fb_init/fb_put_pixel/fb_fill/fb_flush`, on-screen console
- Kernel panic: red screen + UART fallback shell (`help/reboot/poweroff/halt/panic`)
- Power management: `qvos$ poweroff` / `fallback> poweroff` via SBI SRST shutdown

## Build & run
Requirements: clang (with RISC-V target), ld.lld, llvm-objcopy, qemu-system-riscv64,
mtools, make.

    make          # builds kernel + user programs + FAT32 image
    make run      # boots with 2 harts, OpenSBI, virtio-gpu (headless serial)

Interactive shell test:

    { sleep 3; printf 'help\nostest\nexit\n'; sleep 1; } | make run

Panic / fallback shell test:

    { sleep 3; printf 'panic\nhelp\nhalt\n'; sleep 1; } | make run

## Source map
    arch/riscv64/    entry.S, trap.S (uservec/userret trampoline), swtch.S, csr/atomic/trapframe
    kernel/          pmm, vm, proc/scheduler, trap, syscall, fat32, file, elf, uart/klog,
                     framebuffer, panic, ramdisk
    user/            crt0.S, libc, init.c, ostest.c, sh.c
    doc/             architecture plan + bring-up log
