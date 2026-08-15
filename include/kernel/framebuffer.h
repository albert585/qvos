#ifndef QVOS_FRAMEBUFFER_H
#define QVOS_FRAMEBUFFER_H

#include <stdint.h>
#include <stddef.h>

/* 32bpp XRGB framebuffer abstraction. */
int  fb_init(void);          /* 0 on success, -1 if no GPU is present   */
int  fb_active(void);
void fb_put_pixel(uint32_t x, uint32_t y, uint32_t rgb);
void fb_fill(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t rgb);
void fb_flush(void);

void fb_console_clear(uint32_t bg);
void fb_console_write(const char *s, size_t n);

uint32_t fb_width(void);
uint32_t fb_height(void);

#endif
