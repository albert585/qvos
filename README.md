# qvos
> This is an AI project.
> [中文版 README](./README.zh.md)

qvos is the **userspace / rootfs** half of the project. The S-mode kernel is
a separate standalone project under [`plain-b/`](./plain-b/README.md); the
kernel image carries **no root filesystem**, and `/` is a persistent FAT32
volume on the virtio-blk disk (`build/vdisk.img`) mounted at boot like a
Linux `root=/dev/vda` system. Kernel and rootfs are built and versioned
independently.

The kernel feature list below is implemented in `plain-b/`; this tree builds
the user programs and provisions the rootfs.

## What is implemented
- OpenSBI `fw_dynamic` → kernel entry in S-mode (boot hart = whichever hart OpenSBI chooses)
- Unified S-mode trap vector with Sv39 trampoline (S-only trampoline page)
- Full context save/restore: GPRs + FPRs + `v0–v31` (whole-register, VLENB bytes each)
- Timer via Sstc `stimecmp` (SBI TIME kept as fallback)
- Dual-hart SMP: HSM hart start, per-hart data, AMO spinlocks, global RR scheduler
- Sv39 paging: kernel RX/RW split, user U/RWX pages, `sfence.vma`, user/kernel isolation
- DRAM free-list page allocator + first-fit `kmalloc/kfree`
- Processes: fork/exec/wait/exit/yield/sleep, pipe, per-process kernel stacks and `swtch`
- **Decoupled rootfs**: FAT32 on persistent `build/vdisk.img` (16 MiB,
  mtools-provisioned once), BPB/FAT/cluster-chain/directory parser with
  read/write/create/truncate; plain-b.elf is ~530 KiB and contains no disk image
- VFS-lite: console files, FAT files, pipes, devfs; open/read/write/close/
  lseek/fstat/dup/getdents; mount table with `mount("/dev/vda", target)`
  syscall; per-process `cwd` with `chdir`/`getcwd` and relative-path support
- ELF64 RISC-V loader with correct segment permissions and initial argv stack;
  exec goes through the VFS, so relative paths and mounted volumes both work
- Userspace CRT + libc subset (printf/malloc/string/syscall wrappers), plus
  picolibc for pico-linked programs
- `/bin/init` → `/bin/ostest` (all PASS) → `/bin/sh` small shell
  (`help echo cat hd ostest fb vda mount cd pwd panic poweroff exit`)
- virtio-mmio GPU: 32bpp framebuffer, `fb_init/fb_put_pixel/fb_fill/fb_flush`,
  on-screen console
- Userspace framebuffer: `fbmap/fbinfo/fbflush/fbtext/fbclear` syscalls, shared
  U-mode mapping of the GPU backing store, `/bin/fbdemo` animation, `fb` builtin
- Devfs-lite: `/dev/fb0` raw framebuffer device (open/read/write/lseek/fstat),
  plus `/dev/null`, `/dev/zero`, `/dev/mouse`, `/dev/kbd`, `/dev/vda`
- virtio-blk storage: 16 MiB raw `vdisk.img` via virtqueue; both the boot
  rootfs and the `/dev/vda` raw block device with persistent host backing
- virtio-input: QEMU `virtio-tablet-device` + `virtio-keyboard-device`, polled
  eventq drivers, `/dev/mouse` and `/dev/kbd` event streams
  (`/bin/mtest` dumps mouse events; `/bin/term` is a framebuffer terminal)
- virtio-sound: minimal PCM playback through `/dev/pcm` (48 kHz / stereo /
  S16LE), `/bin/play` writes a short sine wave; QEMU captures to
  `/tmp/qvos.wav`
- Directories: `SYS_getdents` + `/bin/ls` list FAT32 directories and the devfs
  `/dev` directory; execve preserves `argc/argv` for spawned programs
- LVGL v9.4 ported to userspace: full-screen XRGB8888 display driver over the
  virtio-gpu framebuffer, built-in heap/string shims, `/bin/lvdemo` animated UI
  with a working mouse pointer (click the OK button to exit)
- toybox v0.8.13 subset linked against picolibc: `/bin/toybox` multiplexer
  runs `echo cat cp ls pwd touch true false yes head tail sleep uname`; the
  shell falls back to it for any command that has no real `/bin/<name>` file
- Kernel panic: red screen + UART fallback shell (`help/reboot/poweroff/halt/panic`)
- Power management: `qvos$ poweroff` / `fallback> poweroff` via SBI SRST shutdown

## Build & run
Requirements: clang (with RISC-V target), ld.lld, llvm-objcopy, qemu-system-riscv64,
mtools, make, meson, ninja.

    make               # build user programs + provision vdisk.img
    make run           # build plain-b kernel if needed, then boot with 2 harts
    make run-gui       # GTK window shows the framebuffer; type in the terminal
    make run-vnc       # headless; connect a VNC viewer to 127.0.0.1:5900+$(VNC_PORT)
    make test          # non-interactive regression boot
    make rootfs-reset  # delete vdisk.img; next make run re-provisions a clean rootfs

Layout:

- `plain-b/` is the standalone kernel project; it produces
  `plain-b/build/plain-b.elf` (~530 KiB, no rootfs inside).
- This tree builds `build/vdisk.img`, a 16 MiB FAT32 rootfs created once.
  Later builds never overwrite it, so files you create inside qvos survive
  rebuilds.
- At boot `plain-b` mounts `/dev/vda` on `/`; if the disk is not a usable
  FAT32 rootfs it panics with
  `VFS: unable to mount root fs on /dev/vda`.

Interactive shell test:

    { sleep 3; printf 'help\nostest\nexit\n'; sleep 1; } | make run

Panic / fallback shell test:

    { sleep 3; printf 'panic\nhelp\nhalt\n'; sleep 1; } | make run

## Root filesystem, mount and cwd

The boot disk is a normal FAT32 image on the host, so it can be managed with
mtools while qvos is not running:

    mdir -i build/vdisk.img ::
    mcopy -i build/vdisk.img /host/path/file ::/file

Inside qvos:

    qvos$ ls                  # lists the current directory
    qvos$ ls /bin
    qvos$ mount /dev/vda /mnt # mount an extra volume (the root disk itself
                              # can be mounted again under /mnt)
    qvos$ cd /mnt
    qvos$ pwd
    qvos$ ls
    qvos$ cat file.txt
    qvos$ cd ..
    qvos$ cd /                # no argument: go to /

Relative paths are resolved by the kernel against the process `cwd`
(`SYS_chdir` / `SYS_getcwd`, syscalls 31/32), so builtins, `/bin/ls`, toybox
applets and exec all share the same current directory. `cwd` is inherited
across `fork` and preserved across `execve`, like POSIX.

Known limitation: the FAT32 driver currently skips LFN (long filename)
entries, so mtools long names are displayed through their uppercase 8.3
alias (for example `hello.txt` shows as `HELLO.TXT`).

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
(`EV_ABS ABS_X/ABS_Y`, `EV_KEY BTN_LEFT`, `EV_SYN`).

## virtio-blk storage
`build/vdisk.img` is a persistent raw 16 MiB image on the host. The kernel
scans the virtio-mmio transport window for device id 2, drives it through a
polled virtqueue, mounts its FAT32 filesystem on `/` at boot and also exposes
it as the raw block device `/dev/vda`:

    qvos$ vda info                 # vda: size=16777216 mode=3
    qvos$ vda write 0x1000 deadbeef
    qvos$ vda read 0x1000          # ef be ad de ... (little-endian, persistent)
    qvos$ vda test                 # 512-byte write/read round-trip

C API (normal file syscalls, no special syscall needed):

    int fd = open("/dev/vda", 2);            /* O_RDWR */
    lseek(fd, off, 0);                       /* SEEK_SET, byte offset */
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
    qvos$ pwd               # builtin now; toybox pwd also works
    qvos$ uname             # qvos
    qvos$ toybox ls /dev    # fb0 kbd mouse null vda zero

The vendored toybox v0.8.13 builds against picolibc. `user/picostubs.c` is the
qvos syscall glue for picolibc, and
`third_party/toybox/qvos_include/pico_compat.h` +
`third_party/toybox/lib/qvos_pico_shim.c` cover the toybox-isms picolibc does
not provide. Enabled applets: `echo cat cp ls pwd touch true false yes head
tail sleep uname`. `cp` and `touch` work because the FAT32 driver now writes
files and allocates clusters.

## Source map
    plain-b/         standalone kernel: arch/riscv64, include/kernel, kernel/
    user/            crt0.S, crt0-pico.S, libc, picostubs, init.c, ostest.c, sh.c,
                     fbdemo.c, ls.c, argtest.c, lvdemo.c, mousetest.c, term.c,
                     play.c, lv_conf.h
    third_party/     picolibc, lvgl v9.4 sources + lv_conf shim, toybox v0.8.13 subset + pico shim
    doc/             architecture plan + bring-up log
