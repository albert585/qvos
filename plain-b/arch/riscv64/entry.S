# qvos S-mode entry.  OpenSBI (fw_dynamic) enters here on every hart:
#   a0 = hartid, a1 = pointer to device-tree blob (we do not parse it in v1)
# All harts start in S-mode.  Only hart 0 runs the bootstrap path;
# hart 1.. parks until the kernel starts it via SBI HSM (Stage 2).
#
#   .set noreorder not used: we rely on normal RISC-V assembler rules.

    .section .text.entry, "ax", @progbits
    .globl _start
    .globl secondary_entry
    .align 3

_start:
    # Exactly one hart may run the bootstrap path, no matter which hart id
    # OpenSBI chooses as its boot hart.  The flag lives in .data (not .bss),
    # so the bss-zeroing loop below does not reset it.
    la      t1, boot_entered
    li      t2, 1
    amoadd.w.aq t3, t2, 0(t1)
    bnez    t3, .Lpark_others

    # hart 0: pick top of the static boot stack area
    la      sp, boot_stack_top
    addi    sp, sp, -16
    andi    sp, sp, -16

    # zero .bss exactly once (hart 0 only, before any C code runs)
    la      t0, _bss_start
    la      t1, _bss_end
1:
    bgeu    t0, t1, 2f
    sd      zero, 0(t0)
    addi    t0, t0, 8
    j       1b
2:
    # tiny C runtime support: clear the frame pointer and return-address cache
    mv      fp, zero
    mv      ra, zero

    tail    kmain                # kmain(a0 = hartid, a1 = dtb) never returns

.Lpark_others:
    wfi
    j       .Lpark_others

# Entry point used by sbi_hart_start(hartid, secondary_entry, opaque) in Stage 2.
# OpenSBI delivers a0 = hartid, a1 = opaque argument.
secondary_entry:
    mv      t0, a0
    la      t1, boot_stacks
    li      t2, 0x10000          # 64 KiB per hart
    mul     t1, t0, t2
    la      t2, boot_stack_top
    sub     sp, t2, t1
    addi    sp, sp, -16
    andi    sp, sp, -16
    mv      a0, t0
    mv      a1, zero
    tail    secondary_main       # Stage 2 provides secondary_main(a0 = hartid)

    .section .data.bootflag, "aw", @progbits
    .align 2
    .globl boot_entered
boot_entered:
    .word 0

    .section .bss
    .align 12
    .globl boot_stacks
boot_stacks:
    .space 0x10000 * 8           # room for 8 harts, 64 KiB each
    .globl boot_stack_top
boot_stack_top:
