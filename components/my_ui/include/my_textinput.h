// my_textinput.h - one entry point for on-screen text entry.
//
// Two widgets share the same callback contract:
//  - CharWheel (my_charwheel.h): scroll-wheel entry, fits narrow screens (320 px portrait)
//  - Qwerty: full Swedish QWERTY (lv_keyboard + lv_textarea) in the shared theme, used when the
//    overlay is at least QWERTY_MIN_W wide (landscape 480+ px panels) or when forced.
// Callers use TextInput::open/close/setHint/text and never need to know which one is shown.
#pragma once

#include <stddef.h>

#include "lvgl.h"
#include "my_charwheel.h"

namespace TextInput {

enum class Mode : uint8_t { AUTO, WHEEL, QWERTY };
constexpr int32_t QWERTY_MIN_W = 480;

// Same contract as CharWheel::Callback: BACK/cancel closes itself, on OK the owner validates
// and calls close() (or setHint() with an error and keeps it open).
using Callback = CharWheel::Callback;

void setDefaultMode(Mode m);   // e.g. from a user setting; AUTO by default
Mode defaultMode();

lv_obj_t *open(lv_obj_t *parent, int32_t x, int32_t y, int32_t w, int32_t h, const char *label,
               const char *initial, size_t maxLen, bool mask, Callback cb, void *user, Mode mode = Mode::AUTO);
void close(lv_obj_t *input);
void setHint(lv_obj_t *input, const char *message);
const char *text(lv_obj_t *input);
bool isQwerty(lv_obj_t *input);

}  // namespace TextInput
