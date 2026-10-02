/* NOVA OS syscall validation fuzz test (Phase 5, T8).
 * Host-side: the REAL kernel/syscall/validate.c against a fake page
 * map. Hand-written edge corpus with exact expectations (bounds,
 * wraparound, caps, holes, supervisor pages, RO-vs-write) plus 20000
 * randomized inputs checked for crash-freedom, errno discipline, and
 * an independent range oracle. Dispatch itself is covered in-guest
 * (probes exercise yield/print/exit/version + counters assert use).
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#include "syscall/syscall.h"
#include "syscall/validate.h"

static int g_failed;

static void check(int cond, const char *what, unsigned long a,
                  unsigned long b) {
    if (!cond) {
        printf("FAIL: %s (%lu, %lu)\n", what, a, b);
        g_failed = 1;
    }
}

/* Fake map: {base, len, present, user, writable}. */
static const struct {
    uint32_t base, len;
    int present, user, writable;
} kMap[] = {
    { 0x80000000u, 0x10000u, 1, 1, 1 },
    { 0x80100000u, 0x1000u, 0, 1, 1 },
    { 0x80200000u, 0x2000u, 1, 1, 0 },
    { 0x80300000u, 0x1000u, 1, 0, 1 },
};
#define NMAP (sizeof(kMap) / sizeof(kMap[0]))

int mem_page_state(uint32_t addr, int *present, int *user, int *writable) {
    uint32_t page = addr & ~0xFFFu;
    if (!present || !user || !writable) {
        return -1;
    }
    *present = 0;
    *user = 0;
    *writable = 0;
    for (uint32_t i = 0; i < NMAP; i++) {
        if (page >= kMap[i].base && page < kMap[i].base + kMap[i].len) {
            *present = kMap[i].present;
            *user = kMap[i].user;
            *writable = kMap[i].writable;
            return 0;
        }
    }
    return 0; /* unmapped hole: absent, rc 0 */
}

/* Independent oracle: every byte in an allowing region? */
static int oracle(uint32_t ptr, uint32_t len, int write) {
    uint64_t end;
    if (len == 0) {
        return 0;
    }
    if (len > SYSCALL_MAX_RANGE) {
        return -NOVA_EINVAL;
    }
    if (ptr < 0x80000000u || ptr >= USER_END) {
        return -NOVA_EFAULT;
    }
    end = (uint64_t)ptr + len;
    if (end <= ptr || end > USER_END) {
        return -NOVA_EFAULT;
    }
    for (uint64_t a = ptr & ~0xFFFULL; a < end; a += 0x1000u) {
        int ok = 0;
        for (uint32_t i = 0; i < NMAP; i++) {
            if (a >= kMap[i].base && a < kMap[i].base + kMap[i].len &&
                kMap[i].present && kMap[i].user &&
                (!write || kMap[i].writable)) {
                ok = 1;
            }
        }
        if (!ok) {
            return -NOVA_EFAULT;
        }
    }
    return 0;
}

static void edge(uint32_t ptr, uint32_t len, int write, int expect,
                 const char *name) {
    int got = validate_usermem(ptr, len, write);
    check(got == expect, name, (unsigned long)got,
          (unsigned long)(uint32_t)expect);
    check(got == 0 || got == -NOVA_EINVAL || got == -NOVA_EFAULT,
          "errno discipline", (unsigned long)got, 0);
}

int main(void) {
    edge(0x80000000u, 100, 0, 0, "basic read");
    edge(0x80000000u, 0, 0, 0, "empty ok");
    edge(0x7FFFFFFFu, 100, 0, -NOVA_EFAULT, "below base");
    edge(0xC0000000u, 100, 0, -NOVA_EFAULT, "at end");
    edge(0xFFFFFFFFu, 100, 0, -NOVA_EFAULT, "top");
    edge(0xFFFFFF00u, 0x200u, 0, -NOVA_EFAULT, "wrap");
    edge(0x80000000u, 5000, 0, -NOVA_EINVAL, "over cap");
    edge(0x80100000u, 100, 0, -NOVA_EFAULT, "hole");
    edge(0x800FF00u, 0x200u, 0, -NOVA_EFAULT, "span into hole");
    edge(0x80200000u, 100, 0, 0, "ro read ok");
    edge(0x80200000u, 100, 1, -NOVA_EFAULT, "ro write");
    edge(0x80300000u, 100, 0, -NOVA_EFAULT, "supervisor");
    edge(0x80000000u, 4096, 0, 0, "exact page");
    edge(0x80000FFFu, 2, 0, 0, "cross page");
    edge(0x80000000u, 100, 1, 0, "basic write");
    srand(20251001);
    for (int i = 0; i < 20000; i++) {
        uint32_t ptr;
        uint32_t len = (uint32_t)rand() % 8192u;
        int write = rand() & 1;
        int r = rand() % 4;
        if (r == 0) {
            ptr = 0x80000000u + (uint32_t)rand() % 0x40000u;
        } else if (r == 1) {
            ptr = 0x80000000u + ((uint32_t)rand() % 0x400) * 0x1000u;
        } else {
            ptr = (uint32_t)rand();
        }
        {
            int got = validate_usermem(ptr, len, write);
            int want = oracle(ptr, len, write);
            check(got == want, "random vs oracle", (unsigned long)got,
                  (unsigned long)(uint32_t)want);
        }
    }
    printf("fuzz: 20000 randomized inputs checked\n");
    if (!g_failed) {
        printf("VERDICT: validation correct\n");
    }
    return g_failed ? 1 : 0;
}
