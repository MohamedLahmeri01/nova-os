#include <assert.h>
#include "../../kernel/platform/device_profile.h"
int main(void)
{
    struct nova_device_profile p = {NOVA_DEVICE_DESKTOP,
        NOVA_INPUT_KEYBOARD | NOVA_INPUT_POINTER, 1920, 1080};
    assert(nova_profile_ui_mode(&p) == NOVA_UI_DESKTOP);
    p.device_class = NOVA_DEVICE_HANDHELD;
    p.input_caps = NOVA_INPUT_GAMEPAD | NOVA_INPUT_TOUCH;
    p.width_px = 1280; p.height_px = 800;
    assert(nova_profile_ui_mode(&p) == NOVA_UI_CONTROLLER);
    p.device_class = NOVA_DEVICE_PHONE;
    p.input_caps = NOVA_INPUT_TOUCH;
    p.width_px = 1080; p.height_px = 2400;
    assert(nova_profile_ui_mode(&p) == NOVA_UI_TOUCH);
    p.input_caps = NOVA_INPUT_KEYBOARD;
    assert(nova_profile_ui_mode(&p) == NOVA_UI_DESKTOP);
    p.device_class = NOVA_DEVICE_DESKTOP;
    p.input_caps = NOVA_INPUT_GAMEPAD;
    assert(nova_profile_ui_mode(&p) == NOVA_UI_CONTROLLER);
    assert(nova_profile_ui_mode(0) == -1);
    p.input_caps = 0; assert(nova_profile_validate(&p) == -1);
    p.input_caps = 0x80000000u; assert(nova_profile_validate(&p) == -1);
    p.input_caps = NOVA_INPUT_TOUCH;
    p.device_class = 0; assert(nova_profile_validate(&p) == -1);
    p.device_class = 4; assert(nova_profile_validate(&p) == -1);
    p.device_class = NOVA_DEVICE_PHONE;
    p.width_px = 159; assert(nova_profile_validate(&p) == -1);
    p.width_px = 8193; assert(nova_profile_validate(&p) == -1);
    p.width_px = 1080; p.height_px = 119;
    assert(nova_profile_validate(&p) == -1);
    p.height_px = 8193; assert(nova_profile_validate(&p) == -1);
    p.height_px = 2400; assert(nova_profile_validate(&p) == 0);
    return 0;
}
