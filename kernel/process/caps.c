/* NOVA OS process capabilities: pure gate/drop logic (Phase 10a).
 * No kernel deps beyond errno macros: host-testable standalone.
 */
#include <stdint.h>

#include "process/caps.h"

int caps_allow(uint32_t caps, uint32_t need) {
    if ((need & ~CAPS_ALL) != 0) {
        return -NOVA_EINVAL;
    }
    if ((caps & need) != need) {
        return -NOVA_EPERM;
    }
    return NOVA_ESUCCESS;
}

int caps_drop(uint32_t caps, uint32_t mask, uint32_t *out) {
    if (out == 0 || (mask & ~CAPS_ALL) != 0) {
        return -NOVA_EINVAL;
    }
    *out = caps & ~mask;
    return NOVA_ESUCCESS;
}
