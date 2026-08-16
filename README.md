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
- Userspace framebuffer: `fbmap/fbinfo/fbflush/fbtext/fbclear` syscalls, shared U-mode
  mapping of the GPU backing store, `/bin/fbdemo` animation, `fb` shell builtin
- Devfs-lite: `/dev/fb0` raw framebuffer device (open/read/write/lseek/fstat),
  plus `/dev/null`, `/dev/zero`, `/dev/mouse`, `/dev/kbd`
- virtio-blk storage: 16 MiB raw `vdisk.img`, sector read/write via virtqueue,
  exposed as the `/dev/vda` block device with persistent host backing
- virtio-input: QEMU `virtio-tablet-device` + `virtio-keyboard-device`, polled
  eventq drivers, `/dev/mouse` and `/dev/kbd` event streams
  (`/bin/mtest` dumps mouse events; `/bin/term` is a framebuffer terminal)
- Directories: `SYS_getdents` + `/bin/ls` list FAT32 directories and the devfs
  `/dev` directory; execve now preserves `argc/argv` for spawned programs
- LVGL v9.4 ported to userspace: full-screen XRGB8888 display driver over the
  virtio-gpu framebuffer, built-in heap/string shims, `/bin/lvdemo` animated UI
  with a working mouse pointer (click the OK button to exit)
- toybox v0.8.13 subset ported to qvos libc: `/bin/toybox` multiplexer runs
  `echo cat ls pwd true false yes head tail sleep uname`; shell falls back to
  it for any command that has no real `/bin/<name>` file
- Kernel panic: red screen + UART fallback shell (`help/reboot/poweroff/halt/panic`)
- Power management: `qvos$ poweroff` / `fallback> poweroff` via SBI SRST shutdown

## Build & run
Requirements: clang (with RISC-V target), ld.lld, llvm-objcopy, qemu-system-riscv64,
mtools, make.

    make          # builds kernel + user programs + FAT32 image
    make run      # boots with 2 harts, OpenSBI, virtio-gpu (headless serial)
    make run-gui  # GTK window shows the framebuffer; type in the terminal
    make run-vnc  # headless; connect a VNC viewer to 127.0.0.1:5900+$(VNC_PORT)

Interactive shell test:

    { sleep 3; printf 'help\nostest\nexit\n'; sleep 1; } | make run

Panic / fallback shell test:

    { sleep 3; printf 'panic\nhelp\nhalt\n'; sleep 1; } | make run

## Directories and ls

    qvos$ ls             # BIN/
    qvos$ ls /bin        # INIT OSTEST SH FBDEMO LS ...
    qvos$ ls /dev        # fb0 vda null zero

`SYS_getdents` fills fixed-size `struct qvos_dirent` records (`d_ino/d_type/
d_namlen/d_name`); `file_readdir()` enumerates FAT32 directory entries or the
virtual `/dev` directory behind the same VFS interface.

## Framebuffer from userspace
The virtio-gpu backing store is mapped read/write into U-mode (fixed VA
`0x30000000`), so any process can draw pixels directly and then flush:

    qvos$ fb info                # 1280x800 32bpp pitch=5120 ...
    qvos$ fb test                # colour bars + gradient + checkerboard
    qvos$ fb text hello          # kernel 8x8 console text on screen
    qvos$ fb clear 202020        # clear with colour 0x00202020
    qvos$ fb dev                 # raw read/write through /dev/fb0
    qvos$ fbdemo                 # /bin/fbdemo animated userspace demo

C API (link with the qvos libc, see `user/fbdemo.c`):

    struct qvos_fb_info info;
    fbinfo(&info);                               // width/height/bpp/pitch/phys
    volatile uint32_t *fb = fbmap();             // 0x30000000, 0 on failure
    fb[y * info.width + x] = 0x00ff0000;         // 0x00RRGGBB, B8G8R8X8
    fbflush();                                   // pixels are invisible until flush

Kernel side: `SYS_fbmap/fbinfo/fbflush/fbtext/fbclear` in `include/kernel/syscall.h`,
dispatch in `kernel/syscall.c`; the shared GPU pages are exempt from the usual
free-on-exit and fork-copies them as a shared mapping (`kernel/vm.c`).

`/dev/fb0` is also a normal character device in the VFS: reads return raw
B8G8R8X8 bytes at the current offset, writes land in the backing store and
auto-flush, `lseek` moves the offset and `fstat` reports the framebuffer size:

    int fd = open("/dev/fb0", 2);            /* O_RDWR */
    lseek(fd, y * info.pitch + x * 4, 0);    /* SEEK_SET */
    write(fd, row, info.pitch);              /* raw pixels, flushed per write */
    read(fd, row_back, info.pitch);          /* raw pixels back */
    close(fd);

`/dev/null` discards writes and `/dev/zero` reads as infinite zero bytes.
`/dev/mouse` returns fixed-size `struct qvos_input_event` records
(`EV_ABS ABS_X/ABS_Y`, `EV_KEY BTN_LEFT`, `EV_SYN`):

## virtio-blk storage
A raw 16 MiB virtual disk (`build/vdisk.img`, auto-created, persisted on the
host) is attached as the second virtio-mmio device.  The kernel scans the
transport window for device id 2, drives it through a polled virtqueue, and
exposes it as the raw block device `/dev/vda`:

    qvos$ vda info                 # vda: size=16777216 mode=3
    qvos$ vda write 0x1000 deadbeef
    qvos$ vda read 0x1000          # ef be ad de ... (little-endian, persistent)
    qvos$ vda test                 # 512-byte write/read round-trip

C API (normal file syscalls, no special syscall needed):

    int fd = open("/dev/vda", 2);            /* O_RDWR */
    lseek(fd, off, 0);                       /* SEEK_SET, sector units bytes */
    write(fd, buf, n);                       /* raw bytes hit the host image */
    read(fd, back, n);                       /* raw bytes back */
    close(fd);

Driver: `kernel/virtioblk.c` (virtio-mmio scan, vring, `VIRTIO_BLK_T_IN/OUT`
with a one-byte status, partial-sector read-modify-write), wired into the VFS
in `kernel/file.c` as `FD_DEVVDA`.

## LVGL
LVGL v9.4 lives in `third_party/lvgl` and is compiled once into
`build/user/liblvgl.a`.  The qvos port (`user/lv_conf.h`) uses 32bpp software
rendering, LVGL's built-in malloc/sprintf shims, no OS:

    qvos$ lvdemo            # animated UI; mouse click the OK button to exit
    qvos$ mtest             # dump raw /dev/mouse events for ~2s

The display driver in `user/lvdemo.c` copies LVGL's XRGB8888 draw buffer into
the shared framebuffer mapping and calls `fbflush()` per frame; the LVGL tick
advances manually each loop iteration.  The pointer indev gets a visible
cursor (cyan square with white border) via `lv_indev_set_cursor()`.

## Framebuffer terminal

    qvos$ term             # 160x100 chars, real shell behind the screen

`/bin/term` is a user-space pseudo-terminal: it forks `/bin/sh`, connects its
stdin/stdout/stderr to two pipes, draws the shell output as an 8x8 text grid
on the framebuffer and feeds `/dev/kbd` keystrokes into the shell.  Commands
typed in the window really execute (`ls`, `cat`, `ostest`, `lvdemo`, ...),
with backspace/tab/enter/shift and screen scrolling; printable output is
mirrored on the UART.

## Toybox subset

    qvos$ toybox            # toybox multiplexer
    qvos$ pwd               # /   (falls back to /bin/toybox)
    qvos$ uname             # qvos
    qvos$ toybox ls /dev    # fb0 kbd mouse null vda zero

The vendored toybox v0.8.13 builds against a freestanding POSIX shim
(`third_party/toybox/qvos_shim.h`, `lib/qvos_shim.c`) over the qvos libc.
Only the read-only-safe applet set is enabled: `echo cat ls pwd true false
yes head tail sleep uname`.  File-writing applets are intentionally absent
because qvos's FAT32 is still read-only.

## Source map
    arch/riscv64/    entry.S, trap.S (uservec/userret trampoline), swtch.S, csr/atomic/trapframe
    kernel/          pmm, vm, proc/scheduler, trap, syscall, fat32, file, elf, uart/klog,
                     framebuffer, virtioblk, virtioinput, virtiokbd, panic, ramdisk
    user/            crt0.S, libc, init.c, ostest.c, sh.c, fbdemo.c, ls.c, argtest.c,
                     lvdemo.c, mousetest.c, term.c, lv_conf.h
    third_party/     lvgl v9.4 sources + lv_conf shim, toybox v0.8.13 subset + qvos shim
    doc/             architecture plan + bring-up log
