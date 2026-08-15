# qvos userspace CRT: kernel delivers a0=argc, a1=argv on the initial
# stack; call main(argc, argv), then exit(main's return value).

    .section .text.entry, "ax", @progbits
    .globl _start
    .align 2

_start:
    mv      s0, a0
    mv      s1, a1
    call    main
    mv      a0, a0          # return value already in a0
    li      a7, 1           # SYS_exit
    ecall
1:  j       1b
