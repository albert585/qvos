# qvos bring-up log

## Stage 0 — toolchain / boot skeleton
Result: **PASS** — clang/ld.lld built `kernel.elf`; OpenSBI handed off in S-mode at
`0x80200000`.
Bugs found and fixed:
1. `mhartid`/`misa` are not readable from S-mode on this OpenSBI/QEMU config —
   use OpenSBI's `a0` hart id and never touch M CSRs.

## Stage 1 — traps / timer / full FP+vector context
Result: **PASS** — S-mode `ecall` self-test returns; periodic timer ticks.
Findings:
1. `sbi_set_timer()` returns success on OpenSBI 1.8.1 + QEMU 11 but produces no
   follow-up interrupt. The delegated Sstc path (`stimecmp`) works; qvos uses
   `stimecmp` directly.
2. `vl` and `vtype` are **read-only** CSRs. Vector context restore recreates
   them with `vsetvl x0, saved_vl, saved_vtype`, then whole-register
   `vl1r.v` loads `v0–v31`.

## Stage 2 — SMP + AMO spinlocks
Result: **PASS** — hart1 boots through SBI HSM; both harts take timer ticks;
20,000-iteration contended spinlock counter == `0x4e20`.
Findings:
1. OpenSBI may choose **hart1** as its boot hart across runs. The entry code
   therefore elects the first arriving hart with an `amoadd` flag in `.data`.
2. A kernel trap must continue on the interrupted kernel stack — resetting to
   the hart stack top corrupts the interrupted C frame.

## Stage 3 — Sv39
Result: **PASS** — `satp=0x8000000000080...`; kernel `.text` is RX (`0xcb`);
UART is RW without X/U; user test page is absent from the kernel table.

## Stage 4 — physical allocator + kmalloc
Result: **PASS** — ~122 MiB free frames; first-fit `kmalloc` with coalescing
self-test OK; page tables and heap pages come from DRAM.

## Stage 5 — processes / scheduler / user traps
Result: **PASS** — real U-mode test runs on the scheduler: fork/wait/yield,
FP `1.5+2.25`, RVV `[11,22,33,44]`.
Findings (important architecture corrections):
1. User trap entry must run from a **trampoline page mapped in the user page
   table**, because `stvec` is fetched under the user `satp`.
2. The trampoline page must **not** carry `PTE_U`: current privileged spec
   forbids S-mode instruction fetch from U pages.
3. The per-process trapframe is mapped at `TRAPFRAME_VA` as an S-only page so
   `uservec` can save context before switching to the kernel `satp`.
4. `userret` restores all GPRs **after** the `satp` switch, through the
   trapframe alias; the process `satp` is passed via `sscratch` for the switch.
5. Kernel tp (`&hart.kern_tf`) and per-process kernel sp are re-armed in the
   trapframe on every return path.

## Stage 6 — FAT32 + VFS + ELF loader
Result: **PASS** — 16 MiB image mounts; `/bin/init` ELF loaded and executed.
Findings:
1. Mid-page PT_LOAD segments must be copied at `vaddr & 0xfff`, not at the raw
   file offset, or the data lands past the page and corrupts page tables.

## Stage 7 — userspace CRT / ostest / shell
Result: **PASS** — `ostest` all PASS; shell `help/echo/ostest/exit` works over
UART. `/bin/init` runs ostest then execs `/bin/sh`.

## Stage 8 — framebuffer + fallback shell
Result: **PASS** — `virtio-gpu-device` on `virtio-mmio-bus.7` (modern
transport, QueueSel/QueueNum/desc/avail/used at spec offsets) gives
1280×800 B8G8R8X8; framebuffer reads back red (`0x00ff0000`) after panic.
Findings:
1. Every virtio-gpu control command needs a 24-byte response descriptor.
2. `SET_SCANOUT`/`TRANSFER_TO_HOST_2D`/`RESOURCE_FLUSH` put the rect **before**
   the ids; guest memory is only visible after transfer-to-host + flush.
3. Panic paints red, mirrors the trap context to UART/fb, then enters
   `fallback> help/reboot/halt/panic`.

## Stage 9 — virtio-blk / virtio-input / LVGL / toybox
Result: **PASS** — `/dev/vda` raw block device with persistent host backing;
`/dev/mouse` + `/dev/kbd` event streams; `lvdemo` animated UI; toybox subset
multiplexer with shell fallback.

## Stage 10 — multi-volume FAT32 + mount + cwd
Result: **PASS** — FAT32 driver parameterized over block backends
(`fat32_mount`), VFS mount table with longest-prefix routing, and
`SYS_mount/SYS_chdir/SYS_getcwd`. `mount /dev/vda /mnt`, `cd /mnt`, `pwd`,
relative-path `ls`/`cat` all work; exec goes through the VFS.

## Stage 11 — decoupled rootfs
Result: **PASS** — the 16 MiB FAT32 image was removed from the kernel
(`kernel/ramdisk.S` deleted). `build/kernel.elf` is ~530 KiB and carries no
rootfs; boot mounts `/dev/vda` as `/` and panics with
`VFS: unable to mount root fs on /dev/vda` when the disk is not a usable
FAT32 volume. `build/vdisk.img` is provisioned once by make and persists
across rebuilds (`make rootfs-reset` to reset).

## Final regression
Command: boot 2 harts with GPU, run `help`, `echo qvos shell ok`, `ostest`,
`panic`, fallback `help`, `halt`.

Observed: `STAGE10_OK`, `ostest RESULT: PASS (0 failures)`, shell echo,
fallback shell prompt, `halted`.
