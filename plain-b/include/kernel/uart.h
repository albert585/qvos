#ifndef QVOS_UART_H
#define QVOS_UART_H

#include <stdint.h>
#include <stddef.h>

void uart_init(void);
void uart_putc(char c);
void uart_puts(const char *s);
int  uart_getc(void);          /* -1 if no character pending */
int  uart_getc_blocking(void); /* waits until a character arrives  */

#endif
