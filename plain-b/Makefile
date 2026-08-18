# plain-b — standalone decoupled RISC-V kernel
#
# This is the decoupled qvos kernel (no embedded rootfs) copied out as an
# independent project.  It builds a pure kernel ELF; the root filesystem is
# the persistent FAT32 vdisk produced by the parent qvos tree.
#
#   make          # build build/plain-b.elf
#   make run      # boot with QEMU using ../build/vdisk.img as root
#   make clean

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

# Rootfs used by `make run` lives in the parent qvos build tree.
VDISK ?= ../build/vdisk.img

QEMU_FLAGS := -machine virt -m 128M -smp 2 -cpu max \
              -bios /usr/share/qemu/opensbi-riscv64-generic-fw_dynamic.bin \
              -device virtio-gpu-device,bus=virtio-mmio-bus.7 \
              -global virtio-mmio.force-legacy=off \
              -drive if=none,file=$(VDISK),format=raw,id=vdisk \
              -device virtio-blk-device,drive=vdisk,bus=virtio-mmio-bus.6 \
              -device virtio-tablet-device,bus=virtio-mmio-bus.5 \
              -device virtio-keyboard-device,bus=virtio-mmio-bus.4 \
              -audiodev wav,id=audio0,path=/tmp/qvos.wav \
              -device virtio-sound-device,bus=virtio-mmio-bus.3,audiodev=audio0 \
              -no-reboot

ARCH_ASM := arch/riscv64/entry.S arch/riscv64/swtch.S arch/riscv64/trap.S
KERNEL_C := $(wildcard kernel/*.c)

OBJS := $(patsubst arch/riscv64/%.S,build/arch/%.o,$(ARCH_ASM)) \
        $(patsubst kernel/%.c,build/kernel/%.o,$(KERNEL_C))

all: build/plain-b.elf

build/arch/%.o: arch/riscv64/%.S
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

build/kernel/%.o: kernel/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

build/plain-b.elf: $(OBJS) arch/riscv64/kernel.ld
	$(LD) $(LDFLAGS) -o $@ $(OBJS)

run: build/plain-b.elf
	$(QEMU) $(QEMU_FLAGS) -nographic -kernel build/plain-b.elf

clean:
	rm -rf build

.PHONY: all run clean
