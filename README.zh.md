# qvos
> 这是一个 AI 项目。
> [English README](./README.md)

qvos 是项目的**用户态 / rootfs 部分**。S-mode 内核在独立的
[`plain-b/`](./plain-b/README.md) 工程中；内核镜像不携带任何根文件系统，
`/` 来自 virtio-blk 磁盘（`build/vdisk.img`）上的持久 FAT32 卷，启动时
挂载，等价于 Linux 的 `root=/dev/vda`。内核与 rootfs 完全解耦，可以独立
构建、独立演进。

下面的内核功能列表都在 `plain-b/` 中实现；本目录负责构建用户程序和生成
rootfs。

## 已实现功能
- OpenSBI `fw_dynamic` → S-mode 内核入口（boot hart 由 OpenSBI 决定）
- 统一 S-mode trap 向量 + Sv39 trampoline（仅 S 权限的 trampoline 页）
- 完整上下文保存/恢复：GPR + FPR + `v0–v31`（整寄存器，各 VLENB 字节）
- Sstc `stimecmp` 定时器（SBI TIME 保留为后备）
- 双核 SMP：HSM hart 启动、per-hart 数据、AMO 自旋锁、全局 RR 调度器
- Sv39 分页：内核 RX/RW 分离、用户 U/RWX 页、`sfence.vma`、用户/内核隔离
- DRAM 空闲链表页分配器 + first-fit `kmalloc/kfree`
- 进程：fork/exec/wait/exit/yield/sleep、pipe、每进程内核栈与 `swtch`
- **解耦的 rootfs**：`build/vdisk.img`（16 MiB，mtools 首次部署）上的持久
  FAT32，支持 BPB/FAT/簇链/目录解析以及读、写、创建、截断；
  `plain-b.elf` 仅约 530 KiB，不含任何磁盘镜像
- VFS-lite：控制台、FAT 文件、管道、devfs；open/read/write/close/lseek/
  fstat/dup/getdents；挂载表 + `mount("/dev/vda", target)` 系统调用；
  每进程 `cwd`，`chdir`/`getcwd`，内核统一解析相对路径
- ELF64 RISC-V 加载器：正确段权限与初始 argv 栈；exec 走 VFS，因此相对
  路径和挂载卷中的程序都能执行
- 用户态 CRT + libc 子集（printf/malloc/string/syscall 封装），另有
  picolibc 供 pico 程序链接
- `/bin/init` → `/bin/ostest`（全部 PASS）→ `/bin/sh` 小 shell
  （内建 `help echo cat hd ostest fb vda mount cd pwd panic poweroff exit`）
- virtio-mmio GPU：32bpp framebuffer、`fb_init/fb_put_pixel/fb_fill/fb_flush`、
  屏上控制台
- 用户态 framebuffer：`fbmap/fbinfo/fbflush/fbtext/fbclear` 系统调用，
  U-mode 共享映射 GPU backing store，`/bin/fbdemo` 动画，`fb` 内建命令
- Devfs-lite：`/dev/fb0` 原始 framebuffer 设备（open/read/write/lseek/fstat），
  另有 `/dev/null`、`/dev/zero`、`/dev/mouse`、`/dev/kbd`、`/dev/vda`
- virtio-blk 存储：16 MiB 原始 `vdisk.img`，virtqueue 轮询驱动；既作为
  启动 rootfs，也以 `/dev/vda` 原始块设备暴露，宿主机持久化
- virtio-input：QEMU `virtio-tablet-device` + `virtio-keyboard-device`，
  轮询 eventq 驱动，`/dev/mouse` 与 `/dev/kbd` 事件流
  （`/bin/mtest` 打印鼠标事件；`/bin/term` 是 framebuffer 终端）
- virtio-sound：通过 `/dev/pcm` 的极简 PCM 播放（48kHz / 双声道 / S16LE），
  `/bin/play` 写入一段短正弦波；QEMU 会把音频捕获到 `/tmp/qvos.wav`
- 目录：`SYS_getdents` + `/bin/ls` 可列 FAT32 目录和虚拟 `/dev` 目录；
  execve 保留新程序的 `argc/argv`
- LVGL v9.4 用户态移植：全屏 XRGB8888 显示驱动、内建堆/字符串 shim、
  `/bin/lvdemo` 动画 UI（鼠标点击 OK 按钮退出）
- toybox v0.8.13 子集，链接 picolibc：`/bin/toybox` 多路复用器支持
  `echo cat cp ls pwd touch true false yes head tail sleep uname`；shell 在
  `/bin/<名字>` 不存在时回退到它
- 内核 panic：红屏 + UART 后备 shell（`help/reboot/poweroff/halt/panic`）
- 电源管理：`qvos$ poweroff` / `fallback> poweroff` 经 SBI SRST 关机

## 构建与运行
依赖：clang（RISC-V target）、ld.lld、llvm-objcopy、qemu-system-riscv64、
mtools、make、meson、ninja。

    make               # 构建用户程序 + 生成 vdisk.img
    make run           # 需要时先构建 plain-b 内核，然后双核启动
    make run-gui       # GTK 窗口显示 framebuffer；在终端输入
    make run-vnc       # 无头模式；用 VNC 连 127.0.0.1:5900+$(VNC_PORT)
    make test          # 非交互回归启动
    make rootfs-reset  # 删除 vdisk.img；下次 make run 重建干净 rootfs

目录结构：

- `plain-b/` 是独立内核工程，产物为 `plain-b/build/plain-b.elf`
  （约 530 KiB，不含 rootfs）。
- 本目录生成 `build/vdisk.img`，只创建一次的 16 MiB FAT32 rootfs。后续
  构建不会覆盖它，qvos 里创建的文件在重新编译后仍然存在。
- 启动时 `plain-b` 把 `/dev/vda` 挂到 `/`；如果磁盘不是可用的 FAT32
  rootfs，则 panic：`VFS: unable to mount root fs on /dev/vda`。

交互式 shell 测试：

    { sleep 3; printf 'help\nostest\nexit\n'; sleep 1; } | make run

Panic / 后备 shell 测试：

    { sleep 3; printf 'panic\nhelp\nhalt\n'; sleep 1; } | make run

## 根文件系统、挂载与当前目录

启动盘是宿主机上一个普通的 FAT32 镜像，qvos 未运行时可以直接用 mtools
维护：

    mdir -i build/vdisk.img ::
    mcopy -i build/vdisk.img /host/path/file ::/file

在 qvos 内：

    qvos$ ls                  # 列出当前目录
    qvos$ ls /bin
    qvos$ mount /dev/vda /mnt # 挂载额外卷（根盘自身也可以再挂到 /mnt）
    qvos$ cd /mnt
    qvos$ pwd
    qvos$ ls
    qvos$ cat file.txt
    qvos$ cd ..
    qvos$ cd /                # 不带参数则回到 /

相对路径由内核根据进程 `cwd` 解析（`SYS_chdir` / `SYS_getcwd`，系统调用
31/32），因此内建命令、`/bin/ls`、toybox applet 和 exec 共享同一个当前
目录。与 POSIX 一致：`fork` 继承 `cwd`，`execve` 保留 `cwd`。

已知限制：FAT32 驱动目前会跳过 LFN（长文件名）条目，所以 mtools 创建的长
文件名会显示成大写 8.3 短名（例如 `hello.txt` 显示为 `HELLO.TXT`）。

## 用户态 framebuffer
virtio-gpu backing store 被以 U-mode 可读写方式映射到固定虚拟地址
`0x30000000`，任何进程都能直接画像素然后 flush：

    qvos$ fb info                # 1280x800 32bpp pitch=5120 ...
    qvos$ fb test                # 色条 + 渐变 + 棋盘格
    qvos$ fb text hello          # 内核 8x8 控制台文本
    qvos$ fb clear 202020        # 用颜色 0x00202020 清屏
    qvos$ fb dev                 # 通过 /dev/fb0 原始读写
    qvos$ fbdemo                 # /bin/fbdemo 动画演示

C API（链接 qvos libc，见 `user/fbdemo.c`）：

    struct qvos_fb_info info;
    fbinfo(&info);                               // width/height/bpp/pitch/phys
    volatile uint32_t *fb = fbmap();             // 0x30000000，失败返回 0
    fb[y * info.width + x] = 0x00ff0000;         // 0x00RRGGBB，B8G8R8X8
    fbflush();                                   // 不 flush 像素不可见

内核侧：`include/kernel/syscall.h` 中
`SYS_fbmap/fbinfo/fbflush/fbtext/fbclear`，`kernel/syscall.c` 分发；共享 GPU
页在进程退出时不回收，fork 时以共享映射复制（`kernel/vm.c`）。

`/dev/fb0` 也是 VFS 中的普通字符设备：read 按当前偏移返回原始 B8G8R8X8
字节，write 写入 backing store 并自动 flush，`lseek` 移动偏移，`fstat` 返回
framebuffer 大小：

    int fd = open("/dev/fb0", 2);            /* O_RDWR */
    lseek(fd, y * info.pitch + x * 4, 0);    /* SEEK_SET */
    write(fd, row, info.pitch);              /* 原始像素，每次写后 flush */
    read(fd, row_back, info.pitch);          /* 读回原始像素 */
    close(fd);

`/dev/null` 丢弃写入；`/dev/zero` 读出无限零字节；`/dev/mouse` 返回定长
`struct qvos_input_event` 记录（`EV_ABS ABS_X/ABS_Y`、`EV_KEY BTN_LEFT`、
`EV_SYN`）。

## virtio-blk 存储
`build/vdisk.img` 是宿主机上的持久原始 16 MiB 镜像。内核扫描 virtio-mmio
传输窗口中的 device id 2，通过轮询 virtqueue 驱动它：启动时把其中的 FAT32
挂到 `/`，同时以原始块设备 `/dev/vda` 暴露：

    qvos$ vda info                 # vda: size=16777216 mode=3
    qvos$ vda write 0x1000 deadbeef
    qvos$ vda read 0x1000          # ef be ad de ...（小端，持久化）
    qvos$ vda test                 # 512 字节写读回环

C API（普通文件系统调用，无需特殊系统调用）：

    int fd = open("/dev/vda", 2);            /* O_RDWR */
    lseek(fd, off, 0);                       /* SEEK_SET，字节偏移 */
    write(fd, buf, n);                       /* 原始字节写入宿主机镜像 */
    read(fd, back, n);                       /* 读回 */
    close(fd);

驱动：`kernel/virtioblk.c`（virtio-mmio 扫描、vring、带单字节 status 的
`VIRTIO_BLK_T_IN/OUT`、跨扇区 read-modify-write），在 `kernel/file.c` 中以
`FD_DEVVDA` 接入 VFS。

## LVGL
LVGL v9.4 位于 `third_party/lvgl`，只编译一次生成
`build/user/liblvgl.a`。qvos 移植（`user/lv_conf.h`）使用 32bpp 软件渲染、
LVGL 内建 malloc/sprintf shim、无 OS：

    qvos$ lvdemo            # 动画 UI；鼠标点击 OK 按钮退出
    qvos$ mtest             # 打印约 2 秒原始 /dev/mouse 事件

`user/lvdemo.c` 的显示驱动把 LVGL 的 XRGB8888 绘制缓冲拷进共享 framebuffer
映射，每帧调用一次 `fbflush()`；LVGL tick 在循环中手动推进。指针 indev 通过
`lv_indev_set_cursor()` 显示可见光标（白边青色方块）。

## Framebuffer 终端

    qvos$ term             # 160x100 字符，屏幕背后是真正的 shell

`/bin/term` 是用户态伪终端：fork `/bin/sh`，把其 stdin/stdout/stderr 接到两
个管道，把 shell 输出以 8x8 文本网格画到 framebuffer，并把 `/dev/kbd` 按键
喂给 shell。窗口中输入的命令真的会执行（`ls`、`cat`、`ostest`、`lvdemo`…），
支持退格/tab/回车/shift 与滚屏；可打印输出同时镜像到 UART。

## toybox 子集

    qvos$ toybox            # toybox 多路复用器
    qvos$ pwd               # 现在也是内建命令；toybox pwd 同样可用
    qvos$ uname             # qvos
    qvos$ toybox ls /dev    # fb0 kbd mouse null vda zero

Vendored toybox v0.8.13 链接 picolibc：`user/picostubs.c` 是 picolibc 的 qvos
系统调用胶水层，`third_party/toybox/qvos_include/pico_compat.h` +
`third_party/toybox/lib/qvos_pico_shim.c` 补齐 picolibc 没有的 toybox 依赖。
已启用 applet：`echo cat cp ls pwd touch true false yes head tail sleep
uname`。因为 FAT32 驱动现在支持写文件与分配簇，`cp` 和 `touch` 可以正常
工作。

## 源码地图
    plain-b/        独立内核：arch/riscv64、include/kernel、kernel/
    user/            crt0.S、crt0-pico.S、libc、picostubs、init.c、ostest.c、
                     sh.c、fbdemo.c、ls.c、argtest.c、lvdemo.c、mousetest.c、
                     term.c、play.c、lv_conf.h
    third_party/     picolibc、lvgl v9.4 源码 + lv_conf shim、
                     toybox v0.8.13 子集 + pico shim
    doc/             架构规划 + bring-up 日志
