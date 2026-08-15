/* Kernel panic: paint the framebuffer red, print the trap context on both
 * consoles, then drop into the built-in fallback shell on the UART.
 */
#include <kernel/framebuffer.h>
#include <kernel/klog.h>
#include <kernel/panic.h>
#include <kernel/sbi.h>
#include <kernel/uart.h>

static int fallback_readline(char *buf, int max)
{
    int n = 0;
    for (;;) {
        int c = uart_getc_blocking();
        if (c == '\r' || c == '\n') {
            buf[n] = 0;
            uart_puts("\r\n");
            return n;
        }
        if ((c == 0x7f || c == 0x08) && n > 0) {
            n--;
            uart_puts("\b \b");
            continue;
        }
        if (n + 1 < max) {
            buf[n++] = (char)c;
            uart_putc((char)c);
        }
    }
}

static int startswith(const char *s, const char *p)
{
    while (*p) {
        if (*s != *p)
            return 0;
        s++;
        p++;
    }
    return 1;
}

__attribute__((noreturn)) void panic(const char *msg, uint64_t cause,
                                     uint64_t stval, uint64_t sepc)
{
    klog_puts("\n[qvos] panic entered\n");
    if (fb_active()) {
        fb_fill(0, 0, fb_width(), fb_height(), 0x00ff0000);  /* red screen */
        fb_console_write("QVOS KERNEL PANIC\n", 18);
    }

    klog_puts("\n[qvos] PANIC: ");
    klog_puts(msg);
    klog_puts("\n[qvos] cause = ");
    klog_hex(cause);
    klog_puts("\n[qvos] stval = ");
    klog_hex(stval);
    klog_puts("\n[qvos] sepc  = ");
    klog_hex(sepc);
    klog_puts("\n[qvos] entering fallback shell\n");

    char line[128];
    for (;;) {
        klog_puts("fallback> ");
        int n = fallback_readline(line, sizeof(line));
        if (n == 0)
            continue;
        if (startswith(line, "help")) {
            klog_puts("help       show this text\n");
            klog_puts("reboot     SBI cold reset\n");
            klog_puts("poweroff   SBI shutdown\n");
            klog_puts("halt       spin forever\n");
            klog_puts("panic      repeat the panic banner\n");
        } else if (startswith(line, "reboot")) {
            klog_puts("rebooting via SBI SRST...\n");
            sbi_system_reset(1, 0);   /* SBI_SRST_RESET_TYPE_COLD_REBOOT */
        } else if (startswith(line, "poweroff")) {
            klog_puts("powering off via SBI SRST...\n");
            sbi_system_shutdown();
            klog_puts("firmware returned from shutdown\n");
        } else if (startswith(line, "halt")) {
            klog_puts("halted\n");
            for (;;)
                __asm__ volatile("wfi");
        } else if (startswith(line, "panic")) {
            klog_puts("panic message: ");
            klog_puts(msg);
            klog_puts("\n");
        } else {
            klog_puts("unknown command, try 'help'\n");
        }
    }
}
