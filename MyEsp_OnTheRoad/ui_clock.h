// ui_clock.h - clock display helper and the manual date/time setting screen (rollers).
#pragma once

#include <lvgl.h>

namespace UiClock {

// Keeps `label` updated with "HH:MM  fre 23 okt" every second (orange + "?" when the time is
// uncertain, "--:--" when unknown). The timer is removed automatically with the label.
void attachLabel(lv_obj_t *label, bool withDate);

// Formats the current local time into buf ("--:--" if unknown). Returns Clock::certain().
bool format(char *buf, size_t cap, bool withDate);

// Full-screen date/time setter. `back` is called (LVGL task) after Spara or Avbryt.
void showSetter(void (*back)());

}  // namespace UiClock
