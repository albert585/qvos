# qvos trap entry/return with an Sv39 trampoline.
#
# stvec -> uservec (inside the .trampoline page, mapped identically in every
# address space).  uservec distinguishes:
#   * kernel trap (sscratch == 0): restore a0 and jump to trap_vector,
#   * user trap  (sscratch == TRAPFRAME_VA): save the full context through
#     the user-mapped trapframe alias, switch satp to the kernel page table,
#     then continue in trap_dispatch_from_user().
#
# trap_return (kernel text) restores FP/vector and all GPRs except a0, then
# jumps to userret in the trampoline; userret loads the process satp, rearms
# sscratch, switches satp and srets.
#
# Trapframe layout must match arch/riscv64/trapframe.h:
#   gpr[32]         0
#   sepc            256
#   sstatus         264
#   scause          272
#   stval           280
#   f[32]           288
#   fcsr            544
#   vcsr            552
#   vl              560
#   vtype           568
#   vstart          576
#   v[32*VLENB]     584
#   kernel_sp       8776
#   kernel_tp       8784
#   satp            8792

    .equ TRAPFRAME_VA, 0x20000000
    .equ GPR_RA,      8
    .equ GPR_SP,      16
    .equ GPR_GP,      24
    .equ GPR_TP,      32
    .equ GPR_T0,      40
    .equ GPR_T1,      48
    .equ GPR_T2,      56
    .equ GPR_S0,      64
    .equ GPR_S1,      72
    .equ GPR_A0,      80
    .equ GPR_A1,      88
    .equ GPR_A2,      96
    .equ GPR_A3,      104
    .equ GPR_A4,      112
    .equ GPR_A5,      120
    .equ GPR_A6,      128
    .equ GPR_A7,      136
    .equ GPR_S2,      144
    .equ GPR_S3,      152
    .equ GPR_S4,      160
    .equ GPR_S5,      168
    .equ GPR_S6,      176
    .equ GPR_S7,      184
    .equ GPR_S8,      192
    .equ GPR_S9,      200
    .equ GPR_S10,     208
    .equ GPR_S11,     216
    .equ GPR_T3,      224
    .equ GPR_T4,      232
    .equ GPR_T5,      240
    .equ GPR_T6,      248
    .equ TF_SEPC,     256
    .equ TF_SSTATUS,  264
    .equ TF_SCAUSE,   272
    .equ TF_STVAL,    280
    .equ TF_F,        288
    .equ TF_FCSR,     544
    .equ TF_VCSR,     552
    .equ TF_VL,       560
    .equ TF_VTYPE,    568
    .equ TF_VSTART,   576
    .equ TF_V,        584
    .equ TF_KSTACK,   8776
    .equ TF_KTP,      8784
    .equ TF_SATP,     8792

    .section .trampoline, "ax", @progbits
    .align 2
    .globl uservec
    .globl userret
    .globl kernel_satp_slot

uservec:
    csrrw   a0, sscratch, a0
    bnez    a0, .Luser_trap

    # ---- trap from S-mode: sscratch was 0; restore kernel a0 and vector
    csrr    a0, sscratch
    csrw    sscratch, zero
    j       trap_vector

.Luser_trap:
    # a0 = TRAPFRAME_VA (mapped in this process's user page table)
    sd      sp, GPR_SP(a0)
    sd      t0, GPR_T0(a0)          # original user t0, still untouched
    csrr    t0, sscratch
    sd      t0, GPR_A0(a0)          # original user a0
    sd      ra, GPR_RA(a0)
    sd      gp, GPR_GP(a0)
    sd      tp, GPR_TP(a0)
    sd      t1, GPR_T1(a0)
    sd      t2, GPR_T2(a0)
    sd      s0, GPR_S0(a0)
    sd      s1, GPR_S1(a0)
    sd      a1, GPR_A1(a0)
    sd      a2, GPR_A2(a0)
    sd      a3, GPR_A3(a0)
    sd      a4, GPR_A4(a0)
    sd      a5, GPR_A5(a0)
    sd      a6, GPR_A6(a0)
    sd      a7, GPR_A7(a0)
    sd      s2, GPR_S2(a0)
    sd      s3, GPR_S3(a0)
    sd      s4, GPR_S4(a0)
    sd      s5, GPR_S5(a0)
    sd      s6, GPR_S6(a0)
    sd      s7, GPR_S7(a0)
    sd      s8, GPR_S8(a0)
    sd      s9, GPR_S9(a0)
    sd      s10, GPR_S10(a0)
    sd      s11, GPR_S11(a0)
    sd      t3, GPR_T3(a0)
    sd      t4, GPR_T4(a0)
    sd      t5, GPR_T5(a0)
    sd      t6, GPR_T6(a0)

    csrr    t0, sepc
    sd      t0, TF_SEPC(a0)
    csrr    t0, sstatus
    sd      t0, TF_SSTATUS(a0)
    csrr    t0, scause
    sd      t0, TF_SCAUSE(a0)
    csrr    t0, stval
    sd      t0, TF_STVAL(a0)
    csrr    t0, satp
    li      t1, TF_SATP
    add     t1, t1, a0
    sd      t0, 0(t1)

    # ---- FP + vector save (user trap: SPP == 0 by construction)
    ld      t0, TF_SSTATUS(a0)
    srli    t1, t0, 13
    andi    t1, t1, 3               # FS
    beqz    t1, 1f
    addi    t1, a0, TF_F
    fsd     f0,  0(t1)
    fsd     f1,  8(t1)
    fsd     f2,  16(t1)
    fsd     f3,  24(t1)
    fsd     f4,  32(t1)
    fsd     f5,  40(t1)
    fsd     f6,  48(t1)
    fsd     f7,  56(t1)
    fsd     f8,  64(t1)
    fsd     f9,  72(t1)
    fsd     f10, 80(t1)
    fsd     f11, 88(t1)
    fsd     f12, 96(t1)
    fsd     f13, 104(t1)
    fsd     f14, 112(t1)
    fsd     f15, 120(t1)
    fsd     f16, 128(t1)
    fsd     f17, 136(t1)
    fsd     f18, 144(t1)
    fsd     f19, 152(t1)
    fsd     f20, 160(t1)
    fsd     f21, 168(t1)
    fsd     f22, 176(t1)
    fsd     f23, 184(t1)
    fsd     f24, 192(t1)
    fsd     f25, 200(t1)
    fsd     f26, 208(t1)
    fsd     f27, 216(t1)
    fsd     f28, 224(t1)
    fsd     f29, 232(t1)
    fsd     f30, 240(t1)
    fsd     f31, 248(t1)
    frcsr   t2
    sd      t2, TF_FCSR(a0)

1:  srli    t1, t0, 9
    andi    t1, t1, 3               # VS
    beqz    t1, 2f
    csrr    t1, vlenb
    addi    t2, a0, TF_V
    csrr    t3, vcsr
    sd      t3, TF_VCSR(a0)
    csrr    t3, vl
    sd      t3, TF_VL(a0)
    csrr    t3, vtype
    sd      t3, TF_VTYPE(a0)
    csrr    t3, vstart
    sd      t3, TF_VSTART(a0)
    vs1r.v  v0,  0(t2)
    add     t2, t2, t1
    vs1r.v  v1,  0(t2)
    add     t2, t2, t1
    vs1r.v  v2,  0(t2)
    add     t2, t2, t1
    vs1r.v  v3,  0(t2)
    add     t2, t2, t1
    vs1r.v  v4,  0(t2)
    add     t2, t2, t1
    vs1r.v  v5,  0(t2)
    add     t2, t2, t1
    vs1r.v  v6,  0(t2)
    add     t2, t2, t1
    vs1r.v  v7,  0(t2)
    add     t2, t2, t1
    vs1r.v  v8,  0(t2)
    add     t2, t2, t1
    vs1r.v  v9,  0(t2)
    add     t2, t2, t1
    vs1r.v  v10, 0(t2)
    add     t2, t2, t1
    vs1r.v  v11, 0(t2)
    add     t2, t2, t1
    vs1r.v  v12, 0(t2)
    add     t2, t2, t1
    vs1r.v  v13, 0(t2)
    add     t2, t2, t1
    vs1r.v  v14, 0(t2)
    add     t2, t2, t1
    vs1r.v  v15, 0(t2)
    add     t2, t2, t1
    vs1r.v  v16, 0(t2)
    add     t2, t2, t1
    vs1r.v  v17, 0(t2)
    add     t2, t2, t1
    vs1r.v  v18, 0(t2)
    add     t2, t2, t1
    vs1r.v  v19, 0(t2)
    add     t2, t2, t1
    vs1r.v  v20, 0(t2)
    add     t2, t2, t1
    vs1r.v  v21, 0(t2)
    add     t2, t2, t1
    vs1r.v  v22, 0(t2)
    add     t2, t2, t1
    vs1r.v  v23, 0(t2)
    add     t2, t2, t1
    vs1r.v  v24, 0(t2)
    add     t2, t2, t1
    vs1r.v  v25, 0(t2)
    add     t2, t2, t1
    vs1r.v  v26, 0(t2)
    add     t2, t2, t1
    vs1r.v  v27, 0(t2)
    add     t2, t2, t1
    vs1r.v  v28, 0(t2)
    add     t2, t2, t1
    vs1r.v  v29, 0(t2)
    add     t2, t2, t1
    vs1r.v  v30, 0(t2)
    add     t2, t2, t1
    vs1r.v  v31, 0(t2)

2:  # ---- switch to the kernel address space, then kernel stack/tp
    li      t0, TF_KSTACK
    add     t0, t0, a0
    ld      t3, 0(t0)               # per-process kernel sp
    li      t0, TF_KTP
    add     t0, t0, a0
    ld      t4, 0(t0)               # hart kern_tf base (tp)
    la      t0, kernel_satp_slot
    ld      t0, 0(t0)
    csrw    satp, t0
    sfence.vma
    mv      tp, t4
    mv      sp, t3
    csrw    sscratch, zero
    j       trap_dispatch_from_user

# userret(TRAPFRAME_VA) — final trampoline leg back to U-mode.
# Called with a0 = TRAPFRAME_VA (the trapframe alias is mapped in the user
# page table as an S-only page).  It switches satp first, then restores the
# whole user context directly through that alias, so no scratch register
# survives the switch and no user GPR is clobbered.
userret:
    csrr    t0, sscratch             # process satp, stashed by trap_return
    csrw    satp, t0
    sfence.vma

    li      t0, TRAPFRAME_VA
    csrw    sscratch, t0

    ld      ra, GPR_RA(a0)
    ld      sp, GPR_SP(a0)
    ld      gp, GPR_GP(a0)
    ld      tp, GPR_TP(a0)
    ld      t0, GPR_T0(a0)
    ld      t1, GPR_T1(a0)
    ld      t2, GPR_T2(a0)
    ld      s0, GPR_S0(a0)
    ld      s1, GPR_S1(a0)
    ld      a1, GPR_A1(a0)
    ld      a2, GPR_A2(a0)
    ld      a3, GPR_A3(a0)
    ld      a4, GPR_A4(a0)
    ld      a5, GPR_A5(a0)
    ld      a6, GPR_A6(a0)
    ld      a7, GPR_A7(a0)
    ld      s2, GPR_S2(a0)
    ld      s3, GPR_S3(a0)
    ld      s4, GPR_S4(a0)
    ld      s5, GPR_S5(a0)
    ld      s6, GPR_S6(a0)
    ld      s7, GPR_S7(a0)
    ld      s8, GPR_S8(a0)
    ld      s9, GPR_S9(a0)
    ld      s10, GPR_S10(a0)
    ld      s11, GPR_S11(a0)
    ld      t3, GPR_T3(a0)
    ld      t4, GPR_T4(a0)
    ld      t5, GPR_T5(a0)
    ld      t6, GPR_T6(a0)
    ld      a0, GPR_A0(a0)
    sret

    .align 3
kernel_satp_slot:
    .dword 0

    .section .text, "ax", @progbits
    .align 2
    .globl trap_vector
    .globl trap_dispatch_from_user
    .globl trap_return

# Kernel-mode trap entry.  Only reached with sscratch == 0.
trap_vector:
    csrrw   tp, sscratch, tp
    bnez    tp, .Lkern_unexpected
    csrr    tp, sscratch            # old kernel tp = &hart->kern_tf
    sd      sp, GPR_SP(tp)
    sd      tp, GPR_TP(tp)
    sd      t0, GPR_T0(tp)
    j       .Lkern_common
.Lkern_unexpected:
    csrw    sscratch, zero
    j       trap_vector
.Lkern_common:
    sd      ra, GPR_RA(tp)
    sd      gp, GPR_GP(tp)
    sd      t1, GPR_T1(tp)
    sd      t2, GPR_T2(tp)
    sd      s0, GPR_S0(tp)
    sd      s1, GPR_S1(tp)
    sd      a0, GPR_A0(tp)
    sd      a1, GPR_A1(tp)
    sd      a2, GPR_A2(tp)
    sd      a3, GPR_A3(tp)
    sd      a4, GPR_A4(tp)
    sd      a5, GPR_A5(tp)
    sd      a6, GPR_A6(tp)
    sd      a7, GPR_A7(tp)
    sd      s2, GPR_S2(tp)
    sd      s3, GPR_S3(tp)
    sd      s4, GPR_S4(tp)
    sd      s5, GPR_S5(tp)
    sd      s6, GPR_S6(tp)
    sd      s7, GPR_S7(tp)
    sd      s8, GPR_S8(tp)
    sd      s9, GPR_S9(tp)
    sd      s10, GPR_S10(tp)
    sd      s11, GPR_S11(tp)
    sd      t3, GPR_T3(tp)
    sd      t4, GPR_T4(tp)
    sd      t5, GPR_T5(tp)
    sd      t6, GPR_T6(tp)

    csrr    t0, sepc
    sd      t0, TF_SEPC(tp)
    csrr    t0, sstatus
    sd      t0, TF_SSTATUS(tp)
    csrr    t0, scause
    sd      t0, TF_SCAUSE(tp)
    csrr    t0, stval
    sd      t0, TF_STVAL(tp)
    csrr    t0, satp
    li      t1, TF_SATP
    add     t1, t1, tp
    sd      t0, 0(t1)

    mv      a0, tp                    # trapframe argument for trap_dispatch
    call    trap_dispatch             # returns trapframe pointer in a0
    j       trap_return

# Continuation for U-mode traps: the trapframe already lives in myproc()->tf.
trap_dispatch_from_user:
    call    trap_dispatch_user_c
    j       trap_return

# trap_return(struct trapframe *tf) — restore everything except a0 and either
# sret (kernel) or jump to userret (user satp switch).
trap_return:
    li      t2, TF_KTP
    add     t2, t2, a0
    sd      tp, 0(t2)                # re-arm kernel tp for the next trap

    ld      t1, TF_SSTATUS(a0)
    srli    t2, t1, 8
    andi    t2, t2, 1               # SPP
    bnez    t2, 1f
    j       2f
1:
    csrw    sscratch, zero
2:
    ld      t2, TF_SEPC(a0)
    csrw    sepc, t2
    # Restore sstatus with SIE cleared; sret itself moves SPIE -> SIE.
    ld      t2, TF_SSTATUS(a0)
    li      t3, -3
    and     t2, t2, t3
    csrw    sstatus, t2

    # ---- optional FP + vector restore, only when returning to U-mode
    ld      t3, TF_SSTATUS(a0)
    srli    t4, t3, 8
    andi    t4, t4, 1
    bnez    t4, 3f

    srli    t4, t3, 13
    andi    t4, t4, 3               # FS
    beqz    t4, 4f
    addi    t4, a0, TF_F
    fld     f0,  0(t4)
    fld     f1,  8(t4)
    fld     f2,  16(t4)
    fld     f3,  24(t4)
    fld     f4,  32(t4)
    fld     f5,  40(t4)
    fld     f6,  48(t4)
    fld     f7,  56(t4)
    fld     f8,  64(t4)
    fld     f9,  72(t4)
    fld     f10, 80(t4)
    fld     f11, 88(t4)
    fld     f12, 96(t4)
    fld     f13, 104(t4)
    fld     f14, 112(t4)
    fld     f15, 120(t4)
    fld     f16, 128(t4)
    fld     f17, 136(t4)
    fld     f18, 144(t4)
    fld     f19, 152(t4)
    fld     f20, 160(t4)
    fld     f21, 168(t4)
    fld     f22, 176(t4)
    fld     f23, 184(t4)
    fld     f24, 192(t4)
    fld     f25, 200(t4)
    fld     f26, 208(t4)
    fld     f27, 216(t4)
    fld     f28, 224(t4)
    fld     f29, 232(t4)
    fld     f30, 240(t4)
    fld     f31, 248(t4)
    ld      t5, TF_FCSR(a0)
    fscsr   t5
4:
    srli    t4, t3, 9
    andi    t4, t4, 3               # VS
    beqz    t4, 3f
    csrr    t4, vlenb
    addi    t5, a0, TF_V
    ld      t6, TF_VCSR(a0)
    csrw    vcsr, t6
    # vl and vtype are read-only CSRs: recreate both with vsetvl.
    ld      t6, TF_VL(a0)
    ld      t3, TF_VTYPE(a0)
    vsetvl  x0, t6, t3
    ld      t6, TF_VSTART(a0)
    csrw    vstart, t6
    vl1r.v  v0,  0(t5)
    add     t5, t5, t4
    vl1r.v  v1,  0(t5)
    add     t5, t5, t4
    vl1r.v  v2,  0(t5)
    add     t5, t5, t4
    vl1r.v  v3,  0(t5)
    add     t5, t5, t4
    vl1r.v  v4,  0(t5)
    add     t5, t5, t4
    vl1r.v  v5,  0(t5)
    add     t5, t5, t4
    vl1r.v  v6,  0(t5)
    add     t5, t5, t4
    vl1r.v  v7,  0(t5)
    add     t5, t5, t4
    vl1r.v  v8,  0(t5)
    add     t5, t5, t4
    vl1r.v  v9,  0(t5)
    add     t5, t5, t4
    vl1r.v  v10, 0(t5)
    add     t5, t5, t4
    vl1r.v  v11, 0(t5)
    add     t5, t5, t4
    vl1r.v  v12, 0(t5)
    add     t5, t5, t4
    vl1r.v  v13, 0(t5)
    add     t5, t5, t4
    vl1r.v  v14, 0(t5)
    add     t5, t5, t4
    vl1r.v  v15, 0(t5)
    add     t5, t5, t4
    vl1r.v  v16, 0(t5)
    add     t5, t5, t4
    vl1r.v  v17, 0(t5)
    add     t5, t5, t4
    vl1r.v  v18, 0(t5)
    add     t5, t5, t4
    vl1r.v  v19, 0(t5)
    add     t5, t5, t4
    vl1r.v  v20, 0(t5)
    add     t5, t5, t4
    vl1r.v  v21, 0(t5)
    add     t5, t5, t4
    vl1r.v  v22, 0(t5)
    add     t5, t5, t4
    vl1r.v  v23, 0(t5)
    add     t5, t5, t4
    vl1r.v  v24, 0(t5)
    add     t5, t5, t4
    vl1r.v  v25, 0(t5)
    add     t5, t5, t4
    vl1r.v  v26, 0(t5)
    add     t5, t5, t4
    vl1r.v  v27, 0(t5)
    add     t5, t5, t4
    vl1r.v  v28, 0(t5)
    add     t5, t5, t4
    vl1r.v  v29, 0(t5)
    add     t5, t5, t4
    vl1r.v  v30, 0(t5)
    add     t5, t5, t4
    vl1r.v  v31, 0(t5)

3:  # ---- decide user vs kernel return BEFORE touching user GPRs
    ld      t3, TF_SSTATUS(a0)
    srli    t4, t3, 8
    andi    t4, t4, 1
    bnez    t4, 5f

    # user return: stash the process satp in sscratch (kernel sscratch is
    # zero here), then userret switches satp and restores GPRs through the
    # TRAPFRAME_VA alias.
    li      t0, TF_SATP
    add     t0, t0, a0
    ld      t0, 0(t0)
    csrw    sscratch, t0
    li      a0, TRAPFRAME_VA
    j       userret

5:  # kernel return: restore every GPR and sret (satp unchanged)
    ld      ra, GPR_RA(a0)
    ld      sp, GPR_SP(a0)
    ld      gp, GPR_GP(a0)
    ld      tp, GPR_TP(a0)
    ld      t0, GPR_T0(a0)
    ld      t1, GPR_T1(a0)
    ld      t2, GPR_T2(a0)
    ld      s0, GPR_S0(a0)
    ld      s1, GPR_S1(a0)
    ld      a1, GPR_A1(a0)
    ld      a2, GPR_A2(a0)
    ld      a3, GPR_A3(a0)
    ld      a4, GPR_A4(a0)
    ld      a5, GPR_A5(a0)
    ld      a6, GPR_A6(a0)
    ld      a7, GPR_A7(a0)
    ld      s2, GPR_S2(a0)
    ld      s3, GPR_S3(a0)
    ld      s4, GPR_S4(a0)
    ld      s5, GPR_S5(a0)
    ld      s6, GPR_S6(a0)
    ld      s7, GPR_S7(a0)
    ld      s8, GPR_S8(a0)
    ld      s9, GPR_S9(a0)
    ld      s10, GPR_S10(a0)
    ld      s11, GPR_S11(a0)
    ld      t3, GPR_T3(a0)
    ld      t4, GPR_T4(a0)
    ld      t5, GPR_T5(a0)
    ld      t6, GPR_T6(a0)
    ld      a0, GPR_A0(a0)
    sret
