// my_fonts.h - user-selectable text size.
// The UI refers to four font roles, my_font_14/18/24/36 (small, body, title, display). Each role is
// a mutable copy of one generated size (fonts/my_font_raw_<px>.c); MyFonts::apply() switches all
// roles together. Screens built after apply() use the new sizes, so callers rebuild the screen.
#pragma once

#include <stdint.h>

#include "lvgl.h"

extern lv_font_t my_font_14;   // Small text role
extern lv_font_t my_font_18;   // Body text role
extern lv_font_t my_font_24;   // Title role
extern lv_font_t my_font_36;   // Display role

namespace MyFonts {

enum class TextSize : uint8_t { SMALL = 0, NORMAL = 1, LARGE = 2, XLARGE = 3, COUNT };

// Pixel sizes of the four roles for each text size (rows: TextSize, columns: 14/18/24/36 role).
constexpr uint8_t ROLE_PX[(int)TextSize::COUNT][4] = {
    {12, 16, 20, 32},
    {14, 18, 24, 36},
    {16, 20, 28, 40},
    {18, 22, 32, 44},
};

constexpr uint16_t REFERENCE_PITCH_UM = 154;
constexpr uint8_t AVAILABLE_PX[] = {12, 14, 16, 18, 20, 22, 24, 28, 32, 36, 40, 44, 48, 52, 56, 60};

// Keep the ES3C35P text sizes as physical targets; select the nearest generated font.
constexpr uint8_t physicalPixels(uint8_t referencePx, uint32_t pitchUm)
{
    if (!pitchUm) pitchUm = REFERENCE_PITCH_UM;
    const uint32_t target = referencePx * REFERENCE_PITCH_UM;
    uint8_t best = AVAILABLE_PX[0];
    uint32_t error = UINT32_MAX;
    for (uint8_t px : AVAILABLE_PX) {
        const uint32_t actual = px * pitchUm;
        const uint32_t distance = actual > target ? actual - target : target - actual;
        if (distance < error) { error = distance; best = px; }
    }
    return best;
}

// Largest text size allowed on a screen whose short side is `shortSide` pixels. XLARGE needs the
// wide (>= 400 px) panels; the 320 px layouts have no room for it.
constexpr TextSize maxFor(int32_t shortSide) { return shortSide >= 400 ? TextSize::XLARGE : TextSize::LARGE; }
constexpr bool valid(uint8_t v) { return v < (uint8_t)TextSize::COUNT; }

// Switches all font roles. Values above the limit for the display are clamped. Call with the
// LVGL lock held (or before LVGL renders) and rebuild the active screen afterwards.
TextSize apply(TextSize size, int32_t shortSide, uint32_t pixelPitchUm = REFERENCE_PITCH_UM);
TextSize current();

}  // namespace MyFonts
