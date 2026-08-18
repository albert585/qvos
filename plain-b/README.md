# plain-b

`plain-b` 是 qvos 解耦内核的独立副本：**纯内核工程，不内嵌 rootfs**。

它从当前 qvos 内核源码复制而来，保留：

- RISC-V 双核 S-mode 内核
- Sv39 分页、进程/调度、syscall、VFS、FAT32 多卷挂载
- virtio-gpu / virtio-blk / virtio-input / virtio-sound 驱动
- `mount` / `chdir` / `getcwd` / 相对路径支持
- `/dev/pcm` 极简 PCM 播放（48kHz / 双声道 / S16LE）

与 qvos 的区别：

- 目录独立、可单独维护
- 构建产物是 `build/plain-b.elf`（约 530 KiB 的纯内核）
- 启动时从 `/dev/vda` 挂载 FAT32 rootfs，根文件系统不在内核里
- 不包含用户程序、LVGL、toybox、picolibc 等用户态部分

## 构建

```bash
make
```

产物：`build/plain-b.elf`

## 运行

默认使用父目录 qvos 工程生成的 rootfs：

```bash
make run
```

等价于：

```bash
qemu-system-riscv64 ... -kernel build/plain-b.elf \
  -drive file=../build/vdisk.img,format=raw,id=vdisk ...
```

如果父目录还没有 `vdisk.img`，先到 `..` 执行：

```bash
make build/vdisk.img
```

`make run` 会附加一个 virtio-sound 设备，并把音频写入 `/tmp/qvos.wav`
（wav 后端）。qvos rootfs 里的 `/bin/play` 可用来验证播放。

## 说明

- rootfs 必须是在 `/dev/vda` 上可用的 FAT32 卷，且包含 `/bin/init`；
  否则内核 panic：
  `VFS: unable to mount root fs on /dev/vda`
- `make clean` 只清理本目录的 `build/`
- 修改内核源码后重新 `make` 即可，rootfs 不会被动过
