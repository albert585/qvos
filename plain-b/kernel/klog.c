/* Kernel logging over the 16550 console, protected by an AMO spinlock so
 * both harts can print without interleaving characters.
 */
#include <kernel/framebuffer.h>
#include <kernel/klog.h>
#include <kernel/uart.h>
#include <kernel/spinlock.h>

static const char hexdig[] = "0123456789abcdef";
static spinlock_t klog_lock;

void klog_init(void)
{
    uart_init();
}

void klog_puts(const char *s)
{
    size_t n = 0;
    while (s[n])
        n++;
    uint64_t flags = spin_lock_irqsave(&klog_lock);
    for (size_t i = 0; i < n; i++)
        uart_putc(s[i]);
    spin_unlock_irqrestore(&klog_lock, flags);
    fb_console_write(s, n);
}

void klog_write(const char *s, size_t n)
{
    uint64_t flags = spin_lock_irqsave(&klog_lock);
    for (size_t i = 0; i < n; i++)
        uart_putc(s[i]);
    spin_unlock_irqrestore(&klog_lock, flags);
    fb_console_write(s, n);
}

void klog_hex(uint64_t v)
{
    char buf[19];
    buf[0] = '0';
    buf[1] = 'x';
    buf[18] = 0;
    for (int i = 17; i >= 2; i--) {
        buf[i] = hexdig[v & 0xf];
        v >>= 4;
    }
    klog_puts(buf);
}
