/* NOVA OS libc: syscall wrappers (Phase 6).
 * Raw INT $0x80 gate: EAX number, EBX/ECX/EDX args, EAX result.
 * Freestanding: only compiler headers (<stdint.h>), no hosted libc.
 */
#ifndef NOVA_LIBC_NOVA_H
#define NOVA_LIBC_NOVA_H

#include <stdint.h>

#define SYS_YIELD 0u
#define SYS_EXIT 1u
#define SYS_PRINT 2u
#define SYS_GETPID 3u
#define SYS_GETTID 4u
#define SYS_VERSION 5u
#define SYS_GETKEY 6u
#define SYS_MEMINFO 7u
#define SYS_TICKS 8u

#define NOVA_EAGAIN 11

static inline int32_t nova_syscall(uint32_t nr, uint32_t a, uint32_t b,
                                   uint32_t c) {
    int32_t ret;
    __asm__ volatile("int $0x80"
                     : "=a"(ret)
                     : "a"(nr), "b"(a), "c"(b), "d"(c)
                     : "memory");
    return ret;
}

static inline int32_t nova_yield(void) {
    return nova_syscall(SYS_YIELD, 0, 0, 0);
}

static inline void nova_exit(int code) {
    nova_syscall(SYS_EXIT, (uint32_t)code, 0, 0);
    for (;;) {
    }
}

static inline int32_t nova_print(const char *s, uint32_t len) {
    return nova_syscall(SYS_PRINT, (uint32_t)s, len, 0);
}

static inline int32_t nova_getpid(void) {
    return nova_syscall(SYS_GETPID, 0, 0, 0);
}

static inline int32_t nova_gettid(void) {
    return nova_syscall(SYS_GETTID, 0, 0, 0);
}

static inline int32_t nova_version(void) {
    return nova_syscall(SYS_VERSION, 0, 0, 0);
}

static inline int32_t nova_getkey(void) {
    return nova_syscall(SYS_GETKEY, 0, 0, 0);
}

static inline int32_t nova_meminfo(uint32_t *total, uint32_t *freep) {
    return nova_syscall(SYS_MEMINFO, (uint32_t)total, (uint32_t)freep, 0);
}

static inline int32_t nova_ticks(void) {
    return nova_syscall(SYS_TICKS, 0, 0, 0);
}

#endif /* NOVA_LIBC_NOVA_H */
