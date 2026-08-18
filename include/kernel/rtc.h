#ifndef QVOS_RTC_H
#define QVOS_RTC_H

#include <stdint.h>

/* QEMU virt machine goldfish RTC at 0x101000.  Returns nanoseconds since
 * the Unix epoch, or 0 if no RTC is present. */
uint64_t rtc_unix_ns(void);
void     rtc_init(void);

#endif
