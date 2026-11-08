/* NOVA OS portable device-profile policy (P1).
 * Pure freestanding C. Profiles describe intended UI, NOT hardware support.
 */
#ifndef NOVA_DEVICE_PROFILE_H
#define NOVA_DEVICE_PROFILE_H
#include <stdint.h>

enum nova_device_class {
    NOVA_DEVICE_DESKTOP = 1, NOVA_DEVICE_HANDHELD = 2,
    NOVA_DEVICE_PHONE = 3
};
enum nova_input_cap {
    NOVA_INPUT_KEYBOARD = 1u << 0,
    NOVA_INPUT_POINTER = 1u << 1,
    NOVA_INPUT_TOUCH = 1u << 2,
    NOVA_INPUT_GAMEPAD = 1u << 3
};
enum nova_ui_mode {
    NOVA_UI_DESKTOP = 1, NOVA_UI_CONTROLLER = 2, NOVA_UI_TOUCH = 3
};
struct nova_device_profile {
    uint32_t device_class;
    uint32_t input_caps;
    uint32_t width_px;
    uint32_t height_px;
};
static inline int nova_profile_validate(const struct nova_device_profile *p)
{
    const uint32_t known = NOVA_INPUT_KEYBOARD | NOVA_INPUT_POINTER |
                           NOVA_INPUT_TOUCH | NOVA_INPUT_GAMEPAD;
    if (!p || p->device_class < NOVA_DEVICE_DESKTOP ||
        p->device_class > NOVA_DEVICE_PHONE)
        return -1;
    if (!p->input_caps || (p->input_caps & ~known))
        return -1;
    if (p->width_px < 160u || p->height_px < 120u ||
        p->width_px > 8192u || p->height_px > 8192u)
        return -1;
    return 0;
}
/* Interaction policy only. Input takes priority over device label. */
static inline int nova_profile_ui_mode(const struct nova_device_profile *p)
{
    if (nova_profile_validate(p) != 0)
        return -1;
    if (p->device_class == NOVA_DEVICE_HANDHELD &&
        (p->input_caps & NOVA_INPUT_GAMEPAD))
        return NOVA_UI_CONTROLLER;
    if (p->device_class == NOVA_DEVICE_PHONE &&
        (p->input_caps & NOVA_INPUT_TOUCH))
        return NOVA_UI_TOUCH;
    if (p->input_caps & (NOVA_INPUT_KEYBOARD | NOVA_INPUT_POINTER))
        return NOVA_UI_DESKTOP;
    if (p->input_caps & NOVA_INPUT_TOUCH)
        return NOVA_UI_TOUCH;
    if (p->input_caps & NOVA_INPUT_GAMEPAD)
        return NOVA_UI_CONTROLLER;
    return -1;
}
#endif
