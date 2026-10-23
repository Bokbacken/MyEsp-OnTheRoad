// encoder_input.h - optional external rotary encoder ("scroll wheel") as an LVGL encoder
// input device (LV_INDEV_TYPE_ENCODER). Pins live in board_pins.h (PIN_ENC_*); the driver
// stays inactive while ENCODER_ENABLED is 0 or the pins are unassigned (-1).
//
// The on-screen touch character wheel (ui_charwheel) works without this hardware.
#pragma once

#include <lvgl.h>

namespace Encoder {
bool begin();                    // Returns true if the encoder is enabled and registered
bool active();
lv_indev_t *indev();
// Binds the encoder to a group of focusable widgets (each screen creates its own group).
void setGroup(lv_group_t *group);
long position();                 // Raw detent count (diagnostics)
}
