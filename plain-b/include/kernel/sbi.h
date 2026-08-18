#ifndef QVOS_SBI_H
#define QVOS_SBI_H

#include <stdint.h>

struct sbiret {
    uint64_t error;
    uint64_t value;
};

#define SBI_SUCCESS              0
#define SBI_ERR_FAILED          -1
#define SBI_ERR_NOT_SUPPORTED   -2
#define SBI_ERR_INVALID_PARAM   -3

#define SBI_EXT_BASE            0x10
#define SBI_EXT_TIME            0x54494D45
#define SBI_EXT_IPI             0x735049
#define SBI_EXT_HSM             0x48534D
#define SBI_EXT_SRST            0x53525354

struct sbiret sbi_ecall(uint64_t ext, uint64_t func,
                        uint64_t a0, uint64_t a1, uint64_t a2,
                        uint64_t a3, uint64_t a4, uint64_t a5);

uint64_t sbi_set_timer(uint64_t stime_value);
void     sbi_send_ipi(uint64_t hart_mask, uint64_t hart_mask_base);
uint64_t sbi_hart_start(uint64_t hartid, uint64_t start_addr, uint64_t opaque);
uint64_t sbi_hart_stop(void);
void     sbi_system_reset(uint32_t reset_type, uint32_t reset_reason);
void     sbi_system_shutdown(void);

#endif
