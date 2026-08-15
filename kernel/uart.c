/* NS16550 UART driver for QEMU virt (0x1000_0000).
 * Polling-only in Stage 0; RX interrupt is deliberately not used so the
 * driver stays usable before PLIC/trap bring-up.
 */
#include <kernel/uart.h>

#define UART_BASE 0x10000000UL

#define UART_RBR 0  /* receiver buffer (read)  */
#define UART_THR 0  /* transmitter holding (w) */
#define UART_IER 1  /* interrupt enable         */
#define UART_FCR 2  /* FIFO control             */
#define UART_LCR 3  /* line control             */
#define UART_MCR 4  /* modem control            */
#define UART_LSR 5  /* line status              */
#define UART_SCR 7  /* scratch                  */

#define LSR_RX_READY  0x01
#define LSR_THR_EMPTY 0x20

static inline uint8_t uart_in(unsigned reg)
{
    volatile uint8_t *p = (volatile uint8_t *)(UART_BASE + reg);
    return *p;
}

static inline void uart_out(unsigned reg, uint8_t val)
{
    volatile uint8_t *p = (volatile uint8_t *)(UART_BASE + reg);
    *p = val;
}

void uart_init(void)
{
    uart_out(UART_IER, 0x00);      /* no interrupts yet            */
    uart_out(UART_FCR, 0x01);      /* enable FIFO                  */
    uart_out(UART_LCR, 0x80);      /* DLAB on                      */
    uart_out(0, 0x01);             /* divisor low  = 1  (38.4k)    */
    uart_out(1, 0x00);             /* divisor high = 0             */
    uart_out(UART_LCR, 0x03);      /* 8N1, DLAB off                */
    uart_out(UART_MCR, 0x03);      /* DTR + RTS                    */
}

void uart_putc(char c)
{
    while (!(uart_in(UART_LSR) & LSR_THR_EMPTY))
        __asm__ volatile("" ::: "memory");
    uart_out(UART_THR, (uint8_t)c);
    if (c == '\n')
        uart_putc('\r');
}

void uart_puts(const char *s)
{
    while (*s)
        uart_putc(*s++);
}

int uart_getc(void)
{
    if (!(uart_in(UART_LSR) & LSR_RX_READY))
        return -1;
    return uart_in(UART_RBR);
}

int uart_getc_blocking(void)
{
    int c;
    while ((c = uart_getc()) < 0)
        __asm__ volatile("" : : : "memory");
    return c;
}
