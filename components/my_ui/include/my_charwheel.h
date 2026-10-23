// ui_charwheel.h - touch "scroll wheel" text entry, ported from the MyPi-Hotspot mypi_kbd
// widget (LVGL 9) to LVGL 8.
//
// Layout: text field, range tabs (az AZ 09 #+ åä), hint row, a 5-row character wheel and
// BACK / DEL / NEXT / OK buttons.
//  - Drag vertically on the wheel to step characters, tap a row to jump to it
//  - Swipe horizontally on the wheel to change range
//  - NEXT appends the centred character, DEL removes the last one
// Touch only - the "wheel" is simulated on screen, no hardware encoder.
// Text logic: mypi_kbd_core.h (identical to MyPi-Hotspot).
#pragma once

#include "lvgl.h"
#include <stddef.h>

namespace CharWheel {

constexpr size_t TEXT_CAP = 128;

// Called on OK (confirmed = true) or BACK (confirmed = false). `text` is a private copy.
// On BACK the widget closes itself; on OK the owner validates and calls close().
typedef void (*Callback)(const char *text, bool confirmed, void *user);

lv_obj_t *create(lv_obj_t *parent, int32_t x, int32_t y, int32_t w, int32_t h,
                 const char *label, const char *initial, size_t maxLen, bool mask,
                 Callback cb, void *user);
void close(lv_obj_t *wheel);                       // Asynchronous delete; wipes the text
void setHint(lv_obj_t *wheel, const char *message); // Temporary message (e.g. validation)
const char *text(lv_obj_t *wheel);

// Pure helpers (also used by tests).
bool utf8Append(char *buf, size_t cap, uint32_t cp);
bool utf8Backspace(char *buf);

}  // namespace CharWheel
