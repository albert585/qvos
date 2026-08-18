# qvos build system — clang/ld.lld cross toolchain (no GNU RISC-V binutils needed)
CC      := clang --target=riscv64-unknown-elf
LD      := ld.lld
AR      := llvm-ar
OBJCOPY := llvm-objcopy
QEMU    := qemu-system-riscv64

MARCH   := rv64imafdcv_zicsr_zifencei
MABI    := lp64d
CMODEL  := medany

WARN    := -Wall -Wextra -Wno-unused-parameter
CFLAGS  := $(WARN) -O2 -g -march=$(MARCH) -mabi=$(MABI) -mcmodel=$(CMODEL) \
           -ffreestanding -fno-builtin -fno-stack-protector -fno-pic -fno-pie \
           -Iinclude -Ikernel -Iarch/riscv64

LDFLAGS := -m elf64lriscv -T arch/riscv64/kernel.ld -nostdlib --gc-sections \
           --build-id=none

VNC_PORT ?= 5
VDISK    := build/vdisk.img
VDISK_MB ?= 16

QEMU_FLAGS := -machine virt -m 128M -smp 2 -cpu max \
              -bios /usr/share/qemu/opensbi-riscv64-generic-fw_dynamic.bin \
              -device virtio-gpu-device,bus=virtio-mmio-bus.7 \
              -global virtio-mmio.force-legacy=off \
              -drive if=none,file=$(VDISK),format=raw,id=vdisk \
              -device virtio-blk-device,drive=vdisk,bus=virtio-mmio-bus.6 \
              -device virtio-tablet-device,bus=virtio-mmio-bus.5 \
              -device virtio-keyboard-device,bus=virtio-mmio-bus.4 \
              -no-reboot

OBJS := build/arch/entry.o \
        build/arch/swtch.o \
        build/arch/trap.o \
        build/kernel/elf.o \
        build/kernel/fat32.o \
        build/kernel/file.o \
        build/kernel/framebuffer.o \
        build/kernel/kmain.o \
        build/kernel/kalloc.o \
        build/kernel/klog.o \
        build/kernel/libk.o \
        build/kernel/panic.o \
        build/kernel/pmm.o \
        build/kernel/proc.o \
        build/kernel/rtc.o \
        build/kernel/sbi.o \
        build/kernel/smp.o \
        build/kernel/spinlock.o \
        build/kernel/stage5_blob.o \
        build/kernel/syscall.o \
        build/kernel/trap.o \
        build/kernel/uart.o \
        build/kernel/virtioblk.o \
        build/kernel/virtioinput.o \
        build/kernel/virtiokbd.o \
        build/kernel/vm.o

all: build/kernel.elf

build/arch/%.o: arch/riscv64/%.S
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

build/kernel/%.o: kernel/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

build/kernel/stage5_blob.o: kernel/stage5_blob.S build/user/stage5.bin
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

# ---- picolibc (cross-compiled once; userspace programs link it for a
# full C library instead of the hand-rolled user/libc.c subset) ----
PICODIR := third_party/picolibc
PICOCFLAGS = $(USERCFLAGS) -I$(PICODIR)/libc/include -Ibuild/picolibc

build/picolibc/libc.a: scripts/cross-qvos.txt
	@mkdir -p $(dir $@)
	@if [ ! -d build/picolibc/meson-private ]; then \
	  meson setup build/picolibc $(PICODIR) --cross-file scripts/cross-qvos.txt \
	    -Dpicocrt=false -Dsemihost=false -Dfake-semihost=false -Dmultilib=false \
	    -Dio-long-long=true -Dthread-local-storage=false -Dnewlib-global-errno=true \
	    -Dsingle-thread=true -Dstdio-exit-flush=true -Dbuildtype=release; \
	else \
	  meson setup --reconfigure build/picolibc $(PICODIR) --cross-file scripts/cross-qvos.txt \
	    -Dpicocrt=false -Dsemihost=false -Dfake-semihost=false -Dmultilib=false \
	    -Dio-long-long=true -Dthread-local-storage=false -Dnewlib-global-errno=true \
	    -Dsingle-thread=true -Dstdio-exit-flush=true -Dbuildtype=release; \
	fi
	ninja -C build/picolibc

build/user/picostubs.o: user/picostubs.c build/picolibc/libc.a
	@mkdir -p $(dir $@)
	$(CC) $(PICOCFLAGS) -c $< -o $@

build/user/hello-pico.o: user/hello-pico.c build/picolibc/libc.a
	@mkdir -p $(dir $@)
	$(CC) $(PICOCFLAGS) -c $< -o $@

build/user/crt0-pico.o: user/crt0-pico.S
	@mkdir -p $(dir $@)
	$(CC) $(USERCFLAGS) -c $< -o $@

build/user/hello-pico.elf: build/user/crt0-pico.o build/user/picostubs.o \
                           build/user/hello-pico.o build/picolibc/libc.a
	$(CC) $(USERCFLAGS) -fuse-ld=lld -Wl,-Ttext=0x40000000 \
	  -Wl,-e,_start -Wl,--build-id=none -o $@ $^

# ---- virtio-blk root disk (persistent FAT32; created once) ----
# The kernel prefers /dev/vda as "/" when it holds a usable FAT32 rootfs.
# The image is only (re)created when missing, so files you write from
# inside qvos survive rebuilds; delete it to re-provision from scratch.
USERBINS := build/user/init.elf build/user/ostest.elf build/user/sh.elf \
            build/user/fbdemo.elf build/user/ls.elf build/user/argtest.elf \
            build/user/lvdemo.elf build/user/mtest.elf build/user/term.elf \
            build/user/toybox.elf build/user/hello-pico.elf build/user/wtest.elf
build/vdisk.img: $(USERBINS)
	dd if=/dev/zero of=$@ bs=1M count=$(VDISK_MB) status=none
	mformat -F -i $@ ::
	mmd -i $@ ::/bin
	mcopy -i $@ build/user/init.elf ::/bin/init
	mcopy -i $@ build/user/ostest.elf ::/bin/ostest
	mcopy -i $@ build/user/sh.elf ::/bin/sh
	mcopy -i $@ build/user/fbdemo.elf ::/bin/fbdemo
	mcopy -i $@ build/user/ls.elf ::/bin/ls
	mcopy -i $@ build/user/argtest.elf ::/bin/argtest
	mcopy -i $@ build/user/lvdemo.elf ::/bin/lvdemo
	mcopy -i $@ build/user/mtest.elf ::/bin/mtest
	mcopy -i $@ build/user/term.elf ::/bin/term
	mcopy -i $@ build/user/toybox.elf ::/bin/toybox
	mcopy -i $@ build/user/hello-pico.elf ::/bin/pico
	mcopy -i $@ build/user/wtest.elf ::/bin/wtest
	@echo "[vdisk] provisioned persistent rootfs at $@ (delete to reset)"

.PHONY: rootfs-reset
rootfs-reset:
	rm -f build/vdisk.img

build/user/stage5.elf: user/stage5_test.c
	@mkdir -p $(dir $@)
	$(CC) -O2 -g -march=$(MARCH) -mabi=$(MABI) -mcmodel=$(CMODEL) \
	  -ffreestanding -fno-builtin -nostdlib -fno-pic -fno-pie \
	  -fuse-ld=lld -Wl,-Ttext=0x40000000 -Wl,-e,_start \
	  -Wl,--build-id=none -o $@ $<

build/user/stage5.bin: build/user/stage5.elf
	$(OBJCOPY) -O binary $< $@

# ---- userspace CRT/libc and programs ----
LVGL_DIR := third_party/lvgl
USERCFLAGS := -O2 -g -march=$(MARCH) -mabi=$(MABI) -mcmodel=$(CMODEL) \
              -ffreestanding -fno-builtin -nostdlib -fno-pic -fno-pie \
              -Iuser -Iinclude -I$(LVGL_DIR) -DLV_CONF_INCLUDE_SIMPLE

build/user/%.o: user/%.c
	@mkdir -p $(dir $@)
	$(CC) $(USERCFLAGS) -c $< -o $@

build/user/%.o: user/%.S
	@mkdir -p $(dir $@)
	$(CC) $(USERCFLAGS) -c $< -o $@

USERLIBS := build/user/crt0.o build/user/libc.o

# ---- LVGL v9 static library (compiled once, linked into /bin/lvdemo) ----
LVGL_CFLAGS := $(USERCFLAGS) -w
LVGL_SRCS   := $(shell find $(LVGL_DIR)/src -name '*.c')
LVGL_OBJS   := $(patsubst $(LVGL_DIR)/src/%.c,build/lvgl/%.o,$(LVGL_SRCS))

build/lvgl/%.o: $(LVGL_DIR)/src/%.c user/lv_conf.h
	@mkdir -p $(dir $@)
	$(CC) $(LVGL_CFLAGS) -c $< -o $@

build/user/liblvgl.a: $(LVGL_OBJS)
	@mkdir -p $(dir $@)
	$(AR) rcs $@ $^

# ---- toybox subset, linked against picolibc (OS glue: user/picostubs.c,
# toybox-isms: third_party/toybox/lib/qvos_pico_shim.c) ----
TOYBOX_DIR := third_party/toybox
PICOTOY_CFLAGS := -O2 -g -march=$(MARCH) -mabi=$(MABI) -mcmodel=$(CMODEL) \
                  -ffreestanding -fno-builtin -nostdlib -fno-pic -fno-pie -w \
                  -ffunction-sections -fdata-sections \
                  -I$(PICODIR)/libc/include -Ibuild/picolibc \
                  -I$(TOYBOX_DIR) -I$(TOYBOX_DIR)/qvos_include -Iinclude

TOYBOX_CORE := main.c lib/lib.c lib/xwrap.c lib/args.c lib/llist.c \
               lib/dirtree.c lib/env.c lib/commas.c lib/utf8.c lib/tty.c
TOYBOX_APPS := toys/posix/cat.c toys/posix/cp.c toys/posix/echo.c \
               toys/posix/false.c toys/posix/head.c toys/posix/ls.c \
               toys/posix/pwd.c toys/posix/sleep.c toys/posix/tail.c \
               toys/posix/touch.c toys/posix/true.c toys/posix/uname.c \
               toys/other/yes.c
PICOTOY_OBJS := $(patsubst %.c,build/toybox-pico/%.o,$(TOYBOX_CORE) $(TOYBOX_APPS)) \
                build/toybox-pico/lib/qvos_pico_shim.o

build/toybox-pico/%.o: $(TOYBOX_DIR)/%.c $(TOYBOX_DIR)/generated/flags.h \
                       $(TOYBOX_DIR)/qvos_include/pico_compat.h \
                       build/picolibc/libc.a
	@mkdir -p $(dir $@)
	$(CC) $(PICOTOY_CFLAGS) -include $(TOYBOX_DIR)/qvos_include/pico_compat.h \
	  -c $< -o $@

build/user/toybox.elf: $(PICOTOY_OBJS) build/user/crt0-pico.o \
                       build/user/picostubs.o build/picolibc/libc.a
	$(CC) $(USERCFLAGS) -fuse-ld=lld -Wl,-Ttext=0x40000000 \
	  -Wl,-e,_start -Wl,--build-id=none -Wl,--gc-sections -o $@ $^

build/user/init.elf: build/user/init.o $(USERLIBS)
	$(CC) $(USERCFLAGS) -fuse-ld=lld -Wl,-Ttext=0x40000000 \
	  -Wl,-e,_start -Wl,--build-id=none -o $@ $^

build/user/ostest.elf: build/user/ostest.o $(USERLIBS)
	$(CC) $(USERCFLAGS) -fuse-ld=lld -Wl,-Ttext=0x40000000 \
	  -Wl,-e,_start -Wl,--build-id=none -o $@ $^

build/user/sh.elf: build/user/sh.o $(USERLIBS)
	$(CC) $(USERCFLAGS) -fuse-ld=lld -Wl,-Ttext=0x40000000 \
	  -Wl,-e,_start -Wl,--build-id=none -o $@ $^

build/user/fbdemo.elf: build/user/fbdemo.o $(USERLIBS)
	$(CC) $(USERCFLAGS) -fuse-ld=lld -Wl,-Ttext=0x40000000 \
	  -Wl,-e,_start -Wl,--build-id=none -o $@ $^

build/user/ls.elf: build/user/ls.o $(USERLIBS)
	$(CC) $(USERCFLAGS) -fuse-ld=lld -Wl,-Ttext=0x40000000 \
	  -Wl,-e,_start -Wl,--build-id=none -o $@ $^

build/user/argtest.elf: build/user/argtest.o $(USERLIBS)
	$(CC) $(USERCFLAGS) -fuse-ld=lld -Wl,-Ttext=0x40000000 \
	  -Wl,-e,_start -Wl,--build-id=none -o $@ $^

build/user/wtest.elf: build/user/wtest.o $(USERLIBS)
	$(CC) $(USERCFLAGS) -fuse-ld=lld -Wl,-Ttext=0x40000000 \
	  -Wl,-e,_start -Wl,--build-id=none -o $@ $^

build/user/lvdemo.elf: build/user/lvdemo.o $(USERLIBS) build/user/liblvgl.a
	$(CC) $(USERCFLAGS) -fuse-ld=lld -Wl,-Ttext=0x40000000 \
	  -Wl,-e,_start -Wl,--build-id=none -o $@ $^

build/user/mtest.elf: build/user/mousetest.o $(USERLIBS)
	$(CC) $(USERCFLAGS) -fuse-ld=lld -Wl,-Ttext=0x40000000 \
	  -Wl,-e,_start -Wl,--build-id=none -o $@ $^

build/user/term.elf: build/user/term.o $(USERLIBS)
	$(CC) $(USERCFLAGS) -fuse-ld=lld -Wl,-Ttext=0x40000000 \
	  -Wl,-e,_start -Wl,--build-id=none -o $@ $^

build/kernel.elf: $(OBJS) arch/riscv64/kernel.ld
	$(LD) $(LDFLAGS) -o $@ $(OBJS)

# Terminal-only mode: UART serial is connected to YOUR terminal's stdin/stdout.
# You can type at qvos$; exit with Ctrl-A x or type 'poweroff'.
run: build/kernel.elf $(VDISK)
	$(QEMU) $(QEMU_FLAGS) -nographic -kernel build/kernel.elf

# Framebuffer window + terminal input in one process:
#   GTK window shows the framebuffer; keep typing in the terminal that ran make.
run-gui: build/kernel.elf $(VDISK)
	$(QEMU) $(QEMU_FLAGS) -display gtk -serial mon:stdio -kernel build/kernel.elf

# Headless framebuffer view: connect a VNC viewer to 127.0.0.1:$(VNC_PORT).
# Type in the terminal that ran make (serial is multiplexed there).
run-vnc: build/kernel.elf $(VDISK)
	$(QEMU) $(QEMU_FLAGS) -display vnc=127.0.0.1:$(VNC_PORT) \
	    -serial mon:stdio -kernel build/kernel.elf

# Non-interactive regression: auto-stop after 15 s, never fails make.
test: build/kernel.elf $(VDISK)
	timeout --foreground 15 $(QEMU) $(QEMU_FLAGS) -nographic \
	    -kernel build/kernel.elf || true

clean:
	rm -rf build

.PHONY: all run run-gui run-vnc test clean
