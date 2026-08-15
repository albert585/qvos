# qvos — Architecture Plan
## RV64IMAFCV dual-core S-mode kernel + POSIX-subset userspace (QEMU virt)

## 1. Hardware model (QEMU riscv64 `virt`, -cpu max)
| Device        | Address                | Notes                                            |
|---------------|------------------------|--------------------------------------------------|
| DRAM          | 0x8000_0000, 128 MiB   | phys memory pool                                |
| UART 16550    | 0x1000_0000            | polling console (TX) + input (RX)               |
| CLINT/ACLINT  | 0x0200_0000            | swi; timer handled via SBI TIME for portability |
| PLIC          | 0x0c00_0000            | SiFive PLIC: priority@0x0, pending@0x1000, enable@0x2000+ctx*0x80, context@0x200000+ctx*0x1000; S context idx = 2*hart+1 |
| virtio-mmio   | 0x1000_1000 + n*0x1000 | n=7 → virtio-gpu, PLIC IRQ = n+1 (IRQ 8)       |
| Boot          | OpenSBI fw_dynamic     | enters kernel @0x8020_0000 in S-mode, a0=hartid, a1=DTB |

## 2. Kernel layout
- Link at 0x8020_0000 (medany).
- Kernel owns physical 0x8000_0000..; stage 4 adds buddy/page allocator above kernel image.
- Sv39: kernel mapped high (0xffffffc0_0000_0000) and identical low (for boot/MMU handover), UART/PLIC/virtio as device (PTE flags RW, no U, no X).
- User address space: 0x0000_0000_0000_0000 .. USER_TOP (ELF PIE/physical-load based), stack at USER_STACK_TOP-…

## 3. Boot sequence
1. OpenSBI (M-mode) → `_start` (S-mode, hart0/hart1).
2. hart0: zero .bss, init console, traps, mmu, allocator, fs, scheduler.
3. hart0 uses SBI HSM `sbi_hart_start(hart1, secondary_entry, arg)`; hart1 sets its per-hart stack/trap CSR and joins scheduler via AMO spinlock.
4. Kernel starts `/bin/init` (ELF64 from FAT32 ramdisk). `/bin/init` runs `/bin/ostest`, then `/bin/sh` (fallback shell).

## 4. Trap model
- `stvec = trap_vector` (direct; all traps share one entry).
- One unified `struct trapframe`: all 32 GPRs, `sepc`, `sstatus`, `scause`, `stval`, `sscratch`; then FPRs `f0-f31 + fcsr` (always), then vector `v0-v31 + vcsr/vl/vtype` (VLENB bytes per reg, always — policy selected by user).
- Kernel traps (scause bit 63=0, sstatus.SPP=1) execute kernel handler; user traps (SPP=0) save FP/vector, dispatch syscall/exception/interrupt, restore state and `sret`.
- Timer: SBI `set_timer`; tick 10 ms.
- External: PLIC claim/complete on each hart; UART input is polled (virtio-gpu IRQ 8 used when enabled).

## 5. SMP
- per-hart struct: hartid, kernel stack, trapframe, current process, tick count, PLIC context.
- `atomic.h` implemented with `amoswap.w.aqrl` / `amoadd` / `amoswap.d`; spinlock with `amoadd` ticket or `amoswap`.
- Scheduler runqueue guarded by a global AMO spinlock.

## 6. Memory management
- Boot allocator → free-list physical page allocator (4 KiB pages, free list of `struct page`).
- kmalloc: power-of-two buddy over pages (small sizes) + whole-page fallback.
- Sv39 pagetable walk/manipulation, `sfence.vma` on every map/unmap; kernel RX/RW split, user RX/RW stacks.

## 7. Scheduler
- `struct process` (pid, parent, pagetable, files, cwd) / `struct thread` (tf, state, quantum).
- Round-robin, 10 ms quantum, per-hart runqueue with global lock; `sched_yield` syscall; idle thread parks in `wfi`.
- fork copies COW-later (v1: deep-copy user pages), execve replaces image, waitpid reaps zombies, kill/exit notify parent.

## 8. Filesystem
- FAT32 image built with mtools into a 16 MiB `uint8_t ramdisk[]` linked into the kernel (stage 6) OR appended binary loaded by QEMU (`-initrd` not needed; kernel-embedded keeps boot simple).
- Parsed on demand: BPB, FAT, cluster chains, root directory. RAM disk is read-mostly; open/read/close/seek + directory enumeration for VFS; write support limited to new files in stage review.
- `/bin/init`, `/bin/ostest`, `/bin/sh` on the image.

## 9. ELF loader
- Validate ELF64 EM_RISCV, load PT_LOAD segments into fresh user pagetable, zero BSS, set user SP/argc/argv, `sret` to entry (`_start` of CRT which calls `main`).

## 10. Userspace
- Freestanding libc subset in `user/`: `crt0.S`, syscall wrappers (ecall ABI: a7=sysno), printf/malloc-lite (sbrk), string functions.
- `/bin/ostest`: fork/exit/wait, pipe, open/read files, mmap-less malloc, float math, RVV dot-product, signals-lite.
- `/bin/sh`: builtin echo/cat/help/ps/exit + spawn programs via fork/exec/wait.

## 11. Framebuffer & fallback shell
- Minimal virtio-mmio driver for device 0x1000_8000 (GPU): negotiate `VIRTIO_GPU_EVENT_DISPLAY`, get scanout, 32bpp XRGB.
- `fb_init/fb_put_pixel/fb_fill`; console mirrors serial to fb; panic paints red screen + message, then drops to fallback shell on UART (fb if alive).

## 12. Syscall subset (standard tier)
process: fork, execve, waitpid, exit, getpid, getppid, sbrk, yield, kill, sleep/nanosleep
fs: open, close, read, write, lseek, fstat, getdents, mkdir, unlink, dup, pipe
misc: time/times, gettimeofday, brk (no-op beyond sbrk), reboot
All are real implementations; no stubs returning ENOSYS in the required set.
