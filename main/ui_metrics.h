// ui_metrics.h - shared UI dimensions/tokens derived from the active display size.
// Values are computed from the live LVGL display (falling back to the hardware profile) and are
// tuned so a 320x480 portrait screen yields the original fixed sizes.
#pragma once
#include <stdint.h>

namespace UiMetrics {

int32_t screenW();
int32_t screenH();

int32_t marginSmall();    // 4 px @320
int32_t marginNormal();   // 10 px @320
int32_t marginLarge();    // 14 px @320

int32_t buttonH();        // standard button height
int32_t buttonWMin();     // minimum comfortable button width
int32_t buttonWMax();     // maximum button width

int32_t cardSpacing();    // gap between stacked cards
int32_t cardMinH();       // minimum card height

int32_t controlSpacing(); // gap between controls / bottom inset
int32_t titleH();         // header bar without the hint row
int32_t headerH();        // area reserved for the header + hint row (top of the content area)
int32_t footerH();        // area reserved for the version footer

}  // namespace UiMetrics
