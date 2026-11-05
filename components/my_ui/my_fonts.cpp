// my_fonts.cpp - see my_fonts.h.
#include "my_fonts.h"

LV_FONT_DECLARE(my_font_raw_10);
LV_FONT_DECLARE(my_font_raw_12);
LV_FONT_DECLARE(my_font_raw_14);
LV_FONT_DECLARE(my_font_raw_16);
LV_FONT_DECLARE(my_font_raw_18);
LV_FONT_DECLARE(my_font_raw_20);
LV_FONT_DECLARE(my_font_raw_22);
LV_FONT_DECLARE(my_font_raw_24);
LV_FONT_DECLARE(my_font_raw_26);
LV_FONT_DECLARE(my_font_raw_28);
LV_FONT_DECLARE(my_font_raw_32);
LV_FONT_DECLARE(my_font_raw_36);
LV_FONT_DECLARE(my_font_raw_40);
LV_FONT_DECLARE(my_font_raw_44);
LV_FONT_DECLARE(my_font_raw_48);
LV_FONT_DECLARE(my_font_raw_52);
LV_FONT_DECLARE(my_font_raw_56);
LV_FONT_DECLARE(my_font_raw_60);
LV_FONT_DECLARE(my_font_raw_64);

// Default (NORMAL) sizes, valid from static initialisation on.
lv_font_t my_font_14 = my_font_raw_14;
lv_font_t my_font_18 = my_font_raw_18;
lv_font_t my_font_24 = my_font_raw_24;
lv_font_t my_font_36 = my_font_raw_36;

namespace MyFonts {

static TextSize s_current = TextSize::NORMAL;
static ScreenZoom s_zoom = ScreenZoom::NORMAL;
static uint32_t s_pitch = 154;

static const lv_font_t *raw(uint8_t px)
{
    switch (px) {
    case 10: return &my_font_raw_10;
    case 12: return &my_font_raw_12;
    case 16: return &my_font_raw_16;
    case 18: return &my_font_raw_18;
    case 20: return &my_font_raw_20;
    case 22: return &my_font_raw_22;
    case 24: return &my_font_raw_24;
    case 26: return &my_font_raw_26;
    case 28: return &my_font_raw_28;
    case 32: return &my_font_raw_32;
    case 36: return &my_font_raw_36;
    case 40: return &my_font_raw_40;
    case 44: return &my_font_raw_44;
    case 48: return &my_font_raw_48;
    case 52: return &my_font_raw_52;
    case 56: return &my_font_raw_56;
    case 60: return &my_font_raw_60;
    case 64: return &my_font_raw_64;
    default: return &my_font_raw_14;
    }
}

TextSize apply(TextSize size, int32_t shortSide, uint32_t pixelPitchUm, ScreenZoom zoom)
{
    if (!valid((uint8_t)size)) size = TextSize::NORMAL;
    const TextSize limit = maxFor(shortSide);
    if ((uint8_t)size > (uint8_t)limit) size = limit;
    if ((uint8_t)zoom >= (uint8_t)ScreenZoom::COUNT) zoom = ScreenZoom::NORMAL;
    s_zoom = zoom;
    s_pitch = pixelPitchUm ? pixelPitchUm : REFERENCE_PITCH_UM;
    const uint16_t percent = zoomPercent(zoom);
    const uint8_t *px = ROLE_PX[(int)size];
    my_font_14 = *raw(physicalPixels(px[0], s_pitch, percent));
    my_font_18 = *raw(physicalPixels(px[1], s_pitch, percent));
    my_font_24 = *raw(physicalPixels(px[2], s_pitch, percent));
    my_font_36 = *raw(physicalPixels(px[3], s_pitch, percent));
    s_current = size;
    return size;
}

TextSize current() { return s_current; }
ScreenZoom currentZoom() { return s_zoom; }

int32_t uiPixels(int32_t referencePx)
{
    // The old P4 metric scale was 132%. Preserve its integer geometry at Normal.
    return p4Pixels(referencePx * 132 / 100);
}

int32_t p4Pixels(int32_t referencePx)
{
    return (referencePx * REFERENCE_PITCH_UM * zoomPercent(s_zoom) + int32_t(s_pitch) * 50) /
           (int32_t(s_pitch) * 100);
}

const lv_font_t *fit(const lv_font_t *preferred, int32_t maxLineHeight)
{
    if (preferred->line_height <= maxLineHeight) return preferred;
    const lv_font_t *best = raw(AVAILABLE_PX[0]);
    for (uint8_t px : AVAILABLE_PX) {
        const lv_font_t *font = raw(px);
        if (font->line_height <= maxLineHeight && font->line_height > best->line_height) best = font;
    }
    return best;
}

}  // namespace MyFonts
