/* NOVA OS libc: syscall wrappers (Phase 6).
 * Raw INT $0x80 gate: EAX number, EBX/ECX/EDX args, EAX result.
 * Freestanding: only compiler headers (<stdint.h>), no hosted libc.
 */
#ifndef NOVA_LIBC_NOVA_H
#define NOVA_LIBC_NOVA_H

#include <stdint.h>

#include "nova/types.h"

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
#define SYS_EXEC 15u
#define SYS_FSCK 16u
#define SYS_PING 17u
#define SYS_DHCP 18u
#define SYS_NETINFO 19u
#define SYS_CAPS 20u
#define SYS_RLIMIT 21u
#define SYS_CRED 22u
#define SYS_CHMOD 23u
#define SYS_CHROOT 24u
#define SYS_FOCUS 25u
#define SYS_WINOPEN 26u
#define SYS_WINSTAT 27u
#define SYS_WINTITLE 28u
#define SYS_WINCLOSE 29u
#define SYS_UNLINK 30u
#define SYS_RMDIR 31u

/* Capability bits + caps ops mirrored from process/caps.h. */
#define CAP_NET 0x01u
#define CAP_SPAWN 0x02u
#define CAP_OP_GET 0u
#define CAP_OP_DROP 1u

/* Rlimit resources + ops mirrored from process/rlimit.h. */
#define RLIM_NOFD 0u
#define RLIM_FSIZE 1u
#define RLIM_FRAMES 2u
#define RLIM_TICKS 3u
#define RLIM_OP_GET 0u
#define RLIM_OP_SET 1u

/* Cred ops mirrored from process/process.h. */
#define CRED_OP_GETUID 0u
#define CRED_OP_GETGID 1u
#define CRED_OP_SETUID 2u

/* Errno subset mirrored from the kernel ABI (negative on return). */
#define NOVA_EHOSTUNREACH 113
#define NOVA_EPERM 1
#define NOVA_EACCES 13

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

static inline int32_t nova_exec(const char *path) {
    return nova_syscall(SYS_EXEC, (uint32_t)path, 0, 0);
}

static inline int32_t nova_fsck(void) {
    return nova_syscall(SYS_FSCK, 0, 0, 0);
}

static inline int32_t nova_ping(uint32_t addr, uint32_t seq) {
    return nova_syscall(SYS_PING, addr, seq, 0);
}

static inline int32_t nova_dhcp(void) {
    return nova_syscall(SYS_DHCP, 0, 0, 0);
}

static inline int32_t nova_netinfo(uint32_t *self, uint32_t *gw,
                                   uint32_t *mask) {
    return nova_syscall(SYS_NETINFO, (uint32_t)self, (uint32_t)gw,
                        (uint32_t)mask);
}

static inline int32_t nova_caps(uint32_t op, uint32_t mask) {
    return nova_syscall(SYS_CAPS, op, mask, 0);
}

static inline int32_t nova_rlimit(uint32_t op, uint32_t which,
                                  uint32_t value) {
    return nova_syscall(SYS_RLIMIT, op, which, value);
}

static inline int32_t nova_cred(uint32_t op, uint32_t id) {
    return nova_syscall(SYS_CRED, op, id, 0);
}

static inline int32_t nova_chmod(const char *path, uint32_t mode) {
    return nova_syscall(SYS_CHMOD, (uint32_t)path, mode, 0);
}

static inline int32_t nova_chroot(const char *path) {
    return nova_syscall(SYS_CHROOT, (uint32_t)path, 0, 0);
}

static inline int32_t nova_unlink(const char *path) {
    return nova_syscall(SYS_UNLINK, (uint32_t)path, 0, 0);
}

static inline int32_t nova_rmdir(const char *path) {
    return nova_syscall(SYS_RMDIR, (uint32_t)path, 0, 0);
}

static inline int32_t nova_winfocus(void) {
    return nova_syscall(SYS_FOCUS, 0, 0, 0);
}

static inline int32_t nova_winopen(const struct nova_winopen *args) {
    return nova_syscall(SYS_WINOPEN, (uint32_t)args, 0, 0);
}

static inline int32_t nova_winstat(struct nova_winstat *buf,
                                   uint32_t max) {
    return nova_syscall(SYS_WINSTAT, (uint32_t)buf, max, 0);
}

static inline int32_t nova_wintitle(uint32_t id, const char *text) {
    return nova_syscall(SYS_WINTITLE, id, (uint32_t)text, 0);
}

static inline int32_t nova_winclose(uint32_t id) {
    return nova_syscall(SYS_WINCLOSE, id, 0, 0);
}

#endif /* NOVA_LIBC_NOVA_H */
