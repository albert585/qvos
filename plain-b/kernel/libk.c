/* Freestanding string/memory functions for the kernel (and, with the same
 * source compiled against the user ABI, for the userspace libc subset). */
#include <kernel/string.h>

void *memcpy(void *dst, const void *src, size_t n)
{
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    for (size_t i = 0; i < n; i++)
        d[i] = s[i];
    return dst;
}

void *memmove(void *dst, const void *src, size_t n)
{
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    if (d < s) {
        for (size_t i = 0; i < n; i++)
            d[i] = s[i];
    } else if (d > s) {
        for (size_t i = n; i > 0; i--)
            d[i - 1] = s[i - 1];
    }
    return dst;
}

void *memset(void *s, int c, size_t n)
{
    uint8_t *p = (uint8_t *)s;
    for (size_t i = 0; i < n; i++)
        p[i] = (uint8_t)c;
    return s;
}

int memcmp(const void *a, const void *b, size_t n)
{
    const uint8_t *x = (const uint8_t *)a;
    const uint8_t *y = (const uint8_t *)b;
    for (size_t i = 0; i < n; i++)
        if (x[i] != y[i])
            return (int)x[i] - (int)y[i];
    return 0;
}

size_t strlen(const char *s)
{
    size_t n = 0;
    while (s[n])
        n++;
    return n;
}

int strcmp(const char *a, const char *b)
{
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return (unsigned char)*a - (unsigned char)*b;
}

int strncmp(const char *a, const char *b, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        if (a[i] != b[i] || a[i] == 0)
            return (unsigned char)a[i] - (unsigned char)b[i];
    }
    return 0;
}

char *strncpy(char *dst, const char *src, size_t n)
{
    size_t i;
    for (i = 0; i < n && src[i]; i++)
        dst[i] = src[i];
    for (; i < n; i++)
        dst[i] = 0;
    return dst;
}
