# qvos build system — clang/ld.lld cross toolchain (no GNU RISC-V binutils needed)
CC      := clang --target=riscv64-unknown-elf
LD      := ld.lld
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

QEMU_FLAGS := -machine virt -m 128M -smp 2 -cpu max \
              -bios /usr/share/qemu/opensbi-riscv64-generic-fw_dynamic.bin \
              -device virtio-gpu-device,bus=virtio-mmio-bus.7 \
              -global virtio-mmio.force-legacy=off \
              -nographic -no-reboot

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
        build/kernel/ramdisk.o \
        build/kernel/sbi.o \
        build/kernel/smp.o \
        build/kernel/spinlock.o \
        build/kernel/stage5_blob.o \
        build/kernel/syscall.o \
        build/kernel/trap.o \
        build/kernel/uart.o \
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

build/kernel/ramdisk.o: kernel/ramdisk.S build/disk.fat32
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

# ---- FAT32 RAM virtual disk (16 MiB) ----
FSDIR := build/fsroot
build/disk.fat32: build/user/init.elf build/user/ostest.elf build/user/sh.elf
	rm -rf $(FSDIR)
	mkdir -p $(FSDIR)/bin
	cp build/user/init.elf $(FSDIR)/bin/init
	cp build/user/ostest.elf $(FSDIR)/bin/ostest
	cp build/user/sh.elf $(FSDIR)/bin/sh
	dd if=/dev/zero of=$@ bs=1M count=16 status=none
	mformat -F -i $@ ::
	mmd -i $@ ::/bin
	mcopy -i $@ $(FSDIR)/bin/init ::/bin/init
	mcopy -i $@ $(FSDIR)/bin/ostest ::/bin/ostest
	mcopy -i $@ $(FSDIR)/bin/sh ::/bin/sh

build/user/stage5.elf: user/stage5_test.c
	@mkdir -p $(dir $@)
	$(CC) -O2 -g -march=$(MARCH) -mabi=$(MABI) -mcmodel=$(CMODEL) \
	  -ffreestanding -fno-builtin -nostdlib -fno-pic -fno-pie \
	  -fuse-ld=lld -Wl,-Ttext=0x40000000 -Wl,-e,_start \
	  -Wl,--build-id=none -o $@ $<

build/user/stage5.bin: build/user/stage5.elf
	$(OBJCOPY) -O binary $< $@

# ---- userspace CRT/libc and programs ----
USERCFLAGS := -O2 -g -march=$(MARCH) -mabi=$(MABI) -mcmodel=$(CMODEL) \
              -ffreestanding -fno-builtin -nostdlib -fno-pic -fno-pie \
              -Iuser -Iinclude

build/user/%.o: user/%.c
	@mkdir -p $(dir $@)
	$(CC) $(USERCFLAGS) -c $< -o $@

build/user/%.o: user/%.S
	@mkdir -p $(dir $@)
	$(CC) $(USERCFLAGS) -c $< -o $@

USERLIBS := build/user/crt0.o build/user/libc.o

build/user/init.elf: build/user/init.o $(USERLIBS)
	$(CC) $(USERCFLAGS) -fuse-ld=lld -Wl,-Ttext=0x40000000 \
	  -Wl,-e,_start -Wl,--build-id=none -o $@ $^

build/user/ostest.elf: build/user/ostest.o $(USERLIBS)
	$(CC) $(USERCFLAGS) -fuse-ld=lld -Wl,-Ttext=0x40000000 \
	  -Wl,-e,_start -Wl,--build-id=none -o $@ $^

build/user/sh.elf: build/user/sh.o $(USERLIBS)
	$(CC) $(USERCFLAGS) -fuse-ld=lld -Wl,-Ttext=0x40000000 \
	  -Wl,-e,_start -Wl,--build-id=none -o $@ $^

build/kernel.elf: $(OBJS) arch/riscv64/kernel.ld
	$(LD) $(LDFLAGS) -o $@ $(OBJS)

# Interactive: serial console is attached to YOUR terminal (make run's stdout).
# Exit with Ctrl-A x, or type 'poweroff' at qvos$.
run: build/kernel.elf
	$(QEMU) $(QEMU_FLAGS) -kernel build/kernel.elf

# Non-interactive regression: auto-stop after 15 s, never fails make.
test: build/kernel.elf
	timeout --foreground 15 $(QEMU) $(QEMU_FLAGS) -kernel build/kernel.elf || true

clean:
	rm -rf build

.PHONY: all run test clean
