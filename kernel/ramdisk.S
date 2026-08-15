# 16 MiB FAT32 RAM virtual disk, built by the Makefile with mtools and
# linked into the kernel.  The FAT32 driver treats it as a byte-addressable
# RAM disk (no block device layer needed for the first phase).

    .section .ramdisk, "aw", @progbits
    .align 12
    .globl ramdisk_start
    .globl ramdisk_end
ramdisk_start:
    .incbin "build/disk.fat32"
ramdisk_end:
