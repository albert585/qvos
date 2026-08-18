/* Goldfish RTC driver for the QEMU riscv64 "virt" machine.
 *
 * Layout (QEMU hw/rtc/goldfish_rtc.c):
 *   0x00 TIME_LOW    low 32 bits of nanoseconds since Unix epoch
 *   0x04 TIME_HIGH   high 32 bits
 * Register reads are plain 32-bit MMIO; the memory is always mapped.
 */
#include <kernel/klog.h>
#include <kernel/rtc.h>

#define RTC_BASE 0x101000UL
#define RTC_TIME_LOW  (RTC_BASE + 0x00)
#define RTC_TIME_HIGH (RTC_BASE + 0x04)

static inline uint32_t mmio32(uint64_t addr)
{
    return *(volatile uint32_t *)addr;
}

uint64_t rtc_unix_ns(void)
{
    uint32_t hi0, lo, hi1;

    /* Guard against the 32-bit low word rolling over mid-read. */
    do {
        hi0 = mmio32(RTC_TIME_HIGH);
        lo  = mmio32(RTC_TIME_LOW);
        hi1 = mmio32(RTC_TIME_HIGH);
    } while (hi0 != hi1);

    return ((uint64_t)hi0 << 32) | lo;
}

void rtc_init(void)
{
    uint64_t ns = rtc_unix_ns();

    klog_puts("[qvos] RTC: ");
    klog_hex(ns);
    klog_puts(" ns since epoch (");
    klog_hex((uint64_t)(ns / 1000000000ULL));
    klog_puts(" s)\n");
}
