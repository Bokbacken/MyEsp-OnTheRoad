// ui_numpad.h - modal 4-digit PIN entry (v0.5.6): big 0-9 / backspace keys (lv_buttonmatrix),
// masked display, OK / Cancel. Fits 320x480 without scrolling; drawn on lv_layer_top() so it
// covers whatever screen is open. LVGL task only.
#pragma once

#include "lvgl.h"

namespace UiNumpad {

// ok=false: cancelled (pin is ""). Return true to close the pad, false to keep it open (e.g.
// after setHint() with an error). The pad is deleted after the callback returns true.
using DoneCb = bool (*)(const char *pin, bool ok, void *user);

// title: first line (font 24), info: smaller text below (may be nullptr). cancelText: label of
// the cancel button (nullptr = "Avbryt"/"Cancel").
lv_obj_t *open(const char *title, const char *info, DoneCb cb, void *user, const char *cancelText = nullptr);
void setHint(lv_obj_t *pad, const char *text, bool error = true);
void close(lv_obj_t *pad);

}  // namespace UiNumpad
