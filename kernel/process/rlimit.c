/* NOVA OS resource limits: pure set-rule + clamp logic (Phase 10b).
 * No kernel deps beyond errno macros: host-testable standalone.
 */
#include <stdint.h>

#include "process/rlimit.h"

int rlimit_set_rule(uint32_t cur, uint32_t val, uint32_t hard,
                    uint32_t *out) {
    if (out == 0 || val > hard) {
        return -NOVA_EINVAL;
    }
    if (val > cur) {
        return -NOVA_EPERM;
    }
    *out = val;
    return NOVA_ESUCCESS;
}

int rlimit_fsize_allow(uint32_t lim, uint32_t off, uint32_t len,
                       uint32_t *oklen) {
    if (oklen == 0) {
        return -NOVA_EINVAL;
    }
    if (off >= lim) {
        return -NOVA_EFBIG;
    }
    *oklen = (len > lim - off) ? lim - off : len;
    return NOVA_ESUCCESS;
}
