/* SBI v0.2+ ecall interface.  OpenSBI runs in M-mode and provides the
 * timer / IPI / HSM services used by the S-mode kernel.
 */
#include <kernel/sbi.h>

struct sbiret sbi_ecall(uint64_t ext, uint64_t func,
                        uint64_t a0, uint64_t a1, uint64_t a2,
                        uint64_t a3, uint64_t a4, uint64_t a5)
{
    register uint64_t ra0 __asm__("a0") = a0;
    register uint64_t ra1 __asm__("a1") = a1;
    register uint64_t ra2 __asm__("a2") = a2;
    register uint64_t ra3 __asm__("a3") = a3;
    register uint64_t ra4 __asm__("a4") = a4;
    register uint64_t ra5 __asm__("a5") = a5;
    register uint64_t ra6 __asm__("a6") = func;
    register uint64_t ra7 __asm__("a7") = ext;
    struct sbiret ret;

    __asm__ volatile("ecall"
                     : "+r"(ra0), "+r"(ra1)
                     : "r"(ra2), "r"(ra3), "r"(ra4),
                       "r"(ra5), "r"(ra6), "r"(ra7)
                     : "memory");

    ret.error = ra0;
    ret.value = ra1;
    return ret;
}

uint64_t sbi_set_timer(uint64_t stime_value)
{
    struct sbiret r = sbi_ecall(SBI_EXT_TIME, 0, stime_value, 0, 0, 0, 0, 0);
    return r.error;
}

void sbi_send_ipi(uint64_t hart_mask, uint64_t hart_mask_base)
{
    sbi_ecall(SBI_EXT_IPI, 0, hart_mask, hart_mask_base, 0, 0, 0, 0);
}

uint64_t sbi_hart_start(uint64_t hartid, uint64_t start_addr, uint64_t opaque)
{
    struct sbiret r = sbi_ecall(SBI_EXT_HSM, 0, hartid, start_addr, opaque, 0, 0, 0);
    return r.error;
}

uint64_t sbi_hart_stop(void)
{
    struct sbiret r = sbi_ecall(SBI_EXT_HSM, 1, 0, 0, 0, 0, 0, 0);
    return r.error;   /* does not return on success */
}

void sbi_system_reset(uint32_t reset_type, uint32_t reset_reason)
{
    sbi_ecall(SBI_EXT_SRST, 0, reset_type, reset_reason, 0, 0, 0, 0);
}

void sbi_system_shutdown(void)
{
    /* SRST reset_type: 0 = shutdown, 1 = cold reboot, 2 = warm reboot. */
    sbi_system_reset(0, 0);
}
