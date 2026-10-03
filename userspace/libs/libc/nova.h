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
#define SYS_OPEN 9u
#define SYS_READ 10u
#define SYS_WRITE 11u
#define SYS_CLOSE 12u
#define SYS_READDIR 13u
#define SYS_MKDIR 14u

#define NOVA_O_RDONLY 0u
#define NOVA_O_WRONLY 1u
#define NOVA_O_RDWR 2u
#define NOVA_O_CREAT 0x40u

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

static inline int32_t nova_open(const char *path, uint32_t flags) {
    return nova_syscall(SYS_OPEN, (uint32_t)path, flags, 0);
}

static inline int32_t nova_read(int fd, void *buf, uint32_t len) {
    return nova_syscall(SYS_READ, (uint32_t)fd, (uint32_t)buf, len);
}

static inline int32_t nova_write(int fd, const void *buf, uint32_t len) {
    return nova_syscall(SYS_WRITE, (uint32_t)fd, (uint32_t)buf, len);
}

static inline int32_t nova_close(int fd) {
    return nova_syscall(SYS_CLOSE, (uint32_t)fd, 0, 0);
}

static inline int32_t nova_readdir(const char *path, uint32_t index,
                                   char *namebuf) {
    return nova_syscall(SYS_READDIR, (uint32_t)path, index,
                        (uint32_t)namebuf);
}

static inline int32_t nova_mkdir(const char *path) {
    return nova_syscall(SYS_MKDIR, (uint32_t)path, 0, 0);
}

#endif /* NOVA_LIBC_NOVA_H */
