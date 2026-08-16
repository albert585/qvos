# qvos userspace CRT for picolibc: run .init_array constructors, then
# main(argc, argv) with the kernel-supplied a0/a1, then exit().
    .section .text.entry, "ax", @progbits
    .globl _start
    .align 2

_start:
    mv      s0, a0
    mv      s1, a1
    call    __libc_init_array
    mv      a0, s0
    mv      a1, s1
    call    main
    call    exit
1:  j       1b
