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
4. Kernel mounts the persistent FAT32 on `/dev/vda` as `/` (no rootfs is embedded in the kernel) and starts `/bin/init` through the VFS. `/bin/init` runs `/bin/ostest`, then `/bin/sh`.

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
- Kernel and rootfs are decoupled: `kernel.elf` contains no disk image.
- The root volume is a 16 MiB FAT32 image (`build/vdisk.img`) provisioned by
  mtools when first created.  `vfs_mount("/dev/vda", "/")` makes it `/`.
- FAT32 driver is backend-agnostic (`fat32_mount(fs, dev, total_bytes,
  read_at, write_at, writable)`); virtio-blk is the boot backend.
- Parsed on demand: BPB, FAT, cluster chains, root directory. open/read/
  write/create/truncate/close/seek + directory enumeration for VFS.
- VFS mount table: longest-prefix match against mount points; devfs routes
  `/dev/*` first; every process has `cwd` and relative paths are resolved in
  `file_open()`.
- `/bin/init`, `/bin/ostest`, `/bin/sh` live on the persistent disk.

## 9. ELF loader
- Validate ELF64 EM_RISCV, load PT_LOAD segments into fresh user pagetable, zero BSS, set user SP/argc/argv, `sret` to entry (`_start` of CRT which calls `main`).

## 10. Userspace
- Freestanding libc subset in `user/`: `crt0.S`, syscall wrappers (ecall ABI: a7=sysno), printf/malloc-lite (sbrk), string functions.
- `/bin/ostest`: fork/exit/wait, pipe, open/read files, mmap-less malloc, float math, RVV dot-product, signals-lite.
- `/bin/sh`: builtins echo/cat/hd/fb/vda/mount/cd/pwd/ostest/panic/poweroff/
  exit + spawn programs via fork/exec/wait. Relative program/file paths work
  through the per-process kernel cwd.
- `/bin/ls`: `SYS_getdents` directory listing; works on FAT32 directories and
  the virtual `/dev` directory. Successful execve keeps the new image's
  argc/argv instead of overwriting a0 with the syscall return value.
- `/bin/argtest`: exit-code probe used by ostest to regression-test that
  execve delivers the expected argc/argv.

## 11. Framebuffer & fallback shell
- Minimal virtio-mmio driver for device 0x1000_8000 (GPU): negotiate `VIRTIO_GPU_EVENT_DISPLAY`, get scanout, 32bpp XRGB.
- `fb_init/fb_put_pixel/fb_fill`; console mirrors serial to fb; panic paints red screen + message, then drops to fallback shell on UART (fb if alive).
- Userspace access: `SYS_fbmap` maps the GPU backing store at fixed VA
  `0x30000000` (U/RW, shared, fork-safe); `SYS_fbflush` pushes changes,
  `SYS_fbinfo/fbtext/fbclear` expose metadata and the kernel 8x8 console.
  `/bin/fbdemo` draws an animated demo straight from U-mode.
- Devfs-lite: `file_open` routes `/dev/*` before FAT32. `/dev/fb0` is a raw
  character device (read = framebuffer bytes at offset, write = pixels +
  auto-flush, fstat = fb size); `/dev/null` and `/dev/zero` round it out.

## 12. virtio-blk storage
- Scans the virtio-mmio window (0x1000_1000..0x1002_0000) for device id 2;
  QEMU attaches the raw `build/vdisk.img` on `virtio-mmio-bus.6`.
- Modern v2 negotiation, one 64-entry virtqueue, polled completion.
  `VIRTIO_BLK_T_IN/OUT` requests carry header + data + one-byte status;
  partial sectors use read-modify-write; capacity is read from config space.
- VFS exposes it as `/dev/vda` (`FD_DEVVDA`): open/read/write/lseek/fstat,
  block-device mode in fstat, persistent across reboots via the host file.
- At boot `vfs_mount("/dev/vda", "/")` makes this volume the root; if it is
  not a usable FAT32 rootfs the kernel panics
  `VFS: unable to mount root fs on /dev/vda`.

## 13. virtio-input mouse/tablet + keyboard
- Scans for device id 18; QEMU attaches `virtio-keyboard-device` on
  `virtio-mmio-bus.4` and `virtio-tablet-device` on bus.5.  `kbd_init()` claims
  the lower slot, `mouse_init()` skips it and claims the next one.
- One 64-entry eventq per device: the driver pre-posts 64 device-writable
  8-byte buffers, polls the used ring, buffers events in a 256-slot FIFO and
  re-posts each descriptor immediately.
- Exposed as `/dev/kbd` and `/dev/mouse` (`FD_DEVKBD/FD_DEVMOUSE`): `read`
  returns packed `struct qvos_input_event` records (`EV_KEY`, `EV_ABS`,
  `EV_SYN`).  `/bin/mtest` dumps mouse events; LVGL's pointer indev consumes
  the tablet (with 0..32767 -> pixel scaling); `/bin/term` runs the real
  `/bin/sh` behind a pipe-based user-space pseudo-terminal and renders its
  stdout on the framebuffer.

## 14. Syscall subset
process: fork, execve, waitpid, exit, getpid, getppid, sbrk, yield, nanosleep
fs: open, close, read, write, lseek, fstat, getdents, dup, pipe,
    mount, chdir, getcwd
misc: gettimeofday, times, reboot
fb: fbmap, fbinfo, fbflush, fbtext, fbclear (shared virtio-gpu framebuffer)
mount/chdir/getcwd back the per-process cwd and the VFS mount table.

## 15. LVGL v9 port
- Sources vendored under `third_party/lvgl`, compiled once into a static
  `liblvgl.a` and linked only into `/bin/lvdemo`.
- `user/lv_conf.h`: `LV_COLOR_DEPTH 32`, software draw, `LV_OS_NONE`,
  built-in malloc/sprintf/string shims, Montserrat 14, asserts/log off.
- Display driver: one full-screen XRGB8888 buffer in qvos user memory;
  `flush_cb` copies it into the `SYS_fbmap` framebuffer and calls
  `SYS_fbflush`; manual tick callback (no clock syscall yet).
- Pointer input: an LVGL `LV_INDEV_TYPE_POINTER` reads `/dev/mouse` in its
  read callback and maps `ABS_X/ABS_Y/BTN_LEFT` to `lv_indev_data_t`;
  clicking the demo button exits `lvdemo`.
- Visible cursor: a 14x14 cyan/white `lv_obj` registered with
  `lv_indev_set_cursor()`, so the pointer position is always visible.

## 16. toybox subset
- toybox v0.8.13 vendored under `third_party/toybox`; enabled applets:
  `echo cat cp ls pwd touch true false yes head tail sleep uname`, plus toybox core.
- Built against picolibc: `user/picostubs.c` is the picolibc OS glue
  (syscalls, stdio, stat, dirent, cwd), and
  `third_party/toybox/qvos_include/pico_compat.h` +
  `third_party/toybox/lib/qvos_pico_shim.c` fill the toybox-isms picolibc
  omits (dev numbers, sendfile, xnotify, syslog, termios helpers).
- `/bin/toybox` links as a multiplexer; qvos shell tries `/bin/<name>` first
  and falls back to `/bin/toybox` with the same argv[0].
- Freestanding shim: `include/inttypes.h` provides the PRI macros LVGL asks
  for on a cross-compiled, no-sysroot toolchain.
