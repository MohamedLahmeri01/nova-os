/* NOVA OS libc: strings (Phase 6). Freestanding, no dependencies. */
#ifndef NOVA_LIBC_STRING_H
#define NOVA_LIBC_STRING_H

#include <stdint.h>

uint32_t nova_strlen(const char *s);
int nova_strcmp(const char *a, const char *b);
int nova_strncmp(const char *a, const char *b, uint32_t n);
void *nova_memcpy(void *dst, const void *src, uint32_t n);
void *nova_memset(void *dst, int c, uint32_t n);
int nova_strtoi(const char *s, int *ok);

#endif /* NOVA_LIBC_STRING_H */
