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
enum class ScreenZoom : uint8_t { SMALL = 0, NORMAL = 1, LARGE = 2, COUNT };
constexpr uint16_t ZOOM_PERCENT[] = {85, 100, 115};
constexpr uint16_t zoomPercent(ScreenZoom zoom)
{
    return (uint8_t)zoom < (uint8_t)ScreenZoom::COUNT ? ZOOM_PERCENT[(uint8_t)zoom] : 100;
}

// P4's pre-zoom physical text sizes are the reference (117 um/px).
// Rows: TextSize; columns: small/body/title/display role.
constexpr uint8_t ROLE_PX[(int)TextSize::COUNT][4] = {
    {16, 20, 28, 44},
    {18, 24, 32, 48},
    {20, 28, 36, 52},
    {24, 28, 44, 56},
};

constexpr uint16_t REFERENCE_PITCH_UM = 117;
constexpr uint8_t AVAILABLE_PX[] = {10, 12, 14, 16, 18, 20, 22, 24, 26, 28, 32, 36, 40, 44, 48, 52, 56, 60, 64};
// Rendered capital-A bitmap heights, verified against the generated font files.
constexpr uint8_t CAP_HEIGHT[] = {7, 9, 10, 12, 13, 14, 16, 17, 18, 20, 22, 25, 29, 31, 34, 37, 39, 42, 45};

// Match visible glyph height, not nominal font pixels or a resolution ratio.
constexpr uint8_t physicalPixels(uint8_t referencePx, uint32_t pitchUm, uint16_t percent = 100)
{
    if (!pitchUm) pitchUm = REFERENCE_PITCH_UM;
    uint8_t referenceCap = referencePx;
    for (unsigned i = 0; i < sizeof(AVAILABLE_PX); ++i)
        if (AVAILABLE_PX[i] == referencePx) referenceCap = CAP_HEIGHT[i];
    const uint32_t target = referenceCap * REFERENCE_PITCH_UM * percent;
    uint8_t best = AVAILABLE_PX[0];
    uint32_t error = UINT32_MAX;
    for (unsigned i = 0; i < sizeof(AVAILABLE_PX); ++i) {
        const uint8_t px = AVAILABLE_PX[i];
        const uint32_t actual = CAP_HEIGHT[i] * pitchUm * 100;
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
TextSize apply(TextSize size, int32_t shortSide, uint32_t pixelPitchUm = REFERENCE_PITCH_UM,
               ScreenZoom zoom = ScreenZoom::NORMAL);
TextSize current();
ScreenZoom currentZoom();
// Legacy UI dimensions are mapped to P4's existing Normal geometry, then to physical size.
int32_t uiPixels(int32_t referencePx);
// Dimensions that were already specified in P4 pixels (shared theme widgets).
int32_t p4Pixels(int32_t referencePx);
// For fixed game cells: prefer the selected role; fit only when the cell cannot contain it.
const lv_font_t *fit(const lv_font_t *preferred, int32_t maxLineHeight);

}  // namespace MyFonts
