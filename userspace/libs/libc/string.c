/* NOVA OS libc: strings (Phase 6). */
#include <stdint.h>

#include "string.h"

uint32_t nova_strlen(const char *s) {
    uint32_t n = 0;
    while (s[n] != 0) {
        n++;
    }
    return n;
}

int nova_strcmp(const char *a, const char *b) {
    while (*a != 0 && *a == *b) {
        a++;
        b++;
    }
    return (int)(unsigned char)*a - (int)(unsigned char)*b;
}

int nova_strncmp(const char *a, const char *b, uint32_t n) {
    for (uint32_t i = 0; i < n; i++) {
        if (a[i] != b[i] || a[i] == 0) {
            return (int)(unsigned char)a[i] - (int)(unsigned char)b[i];
        }
    }
    return 0;
}

void *nova_memcpy(void *dst, const void *src, uint32_t n) {
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    for (uint32_t i = 0; i < n; i++) {
        d[i] = s[i];
    }
    return dst;
}

void *nova_memset(void *dst, int c, uint32_t n) {
    uint8_t *d = (uint8_t *)dst;
    for (uint32_t i = 0; i < n; i++) {
        d[i] = (uint8_t)c;
    }
    return dst;
}

int nova_strtoi(const char *s, int *ok) {
    int neg = 0;
    int v = 0;
    if (*s == '-') {
        neg = 1;
        s++;
    }
    if (*s == 0) {
        *ok = 0;
        return 0;
    }
    while (*s != 0) {
        if (*s < '0' || *s > '9') {
            *ok = 0;
            return 0;
        }
        v = v * 10 + (*s - '0');
        s++;
    }
    *ok = 1;
    return neg ? -v : v;
}
