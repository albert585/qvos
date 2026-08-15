# Stage 5 raw user test image, assembled and objcopy'd from user/stage5_test.c.
# The ELF loader (Stage 6) replaces this hand-loaded binary.

    .section .rodata, "a", @progbits
    .align 3
    .globl stage5_blob_start
    .globl stage5_blob_end
stage5_blob_start:
    .incbin "build/user/stage5.bin"
stage5_blob_end:
