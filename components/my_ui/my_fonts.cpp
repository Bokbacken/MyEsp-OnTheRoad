// my_fonts.cpp - see my_fonts.h.
#include "my_fonts.h"

LV_FONT_DECLARE(my_font_raw_12);
LV_FONT_DECLARE(my_font_raw_14);
LV_FONT_DECLARE(my_font_raw_16);
LV_FONT_DECLARE(my_font_raw_18);
LV_FONT_DECLARE(my_font_raw_20);
LV_FONT_DECLARE(my_font_raw_22);
LV_FONT_DECLARE(my_font_raw_24);
LV_FONT_DECLARE(my_font_raw_28);
LV_FONT_DECLARE(my_font_raw_32);
LV_FONT_DECLARE(my_font_raw_36);
LV_FONT_DECLARE(my_font_raw_40);
LV_FONT_DECLARE(my_font_raw_44);

// Default (NORMAL) sizes, valid from static initialisation on.
lv_font_t my_font_14 = my_font_raw_14;
lv_font_t my_font_18 = my_font_raw_18;
lv_font_t my_font_24 = my_font_raw_24;
lv_font_t my_font_36 = my_font_raw_36;

namespace MyFonts {

static TextSize s_current = TextSize::NORMAL;

static const lv_font_t *raw(uint8_t px)
{
    switch (px) {
    case 12: return &my_font_raw_12;
    case 16: return &my_font_raw_16;
    case 18: return &my_font_raw_18;
    case 20: return &my_font_raw_20;
    case 22: return &my_font_raw_22;
    case 24: return &my_font_raw_24;
    case 28: return &my_font_raw_28;
    case 32: return &my_font_raw_32;
    case 36: return &my_font_raw_36;
    case 40: return &my_font_raw_40;
    case 44: return &my_font_raw_44;
    default: return &my_font_raw_14;
    }
}

TextSize apply(TextSize size, int32_t shortSide)
{
    if (!valid((uint8_t)size)) size = TextSize::NORMAL;
    const TextSize limit = maxFor(shortSide);
    if ((uint8_t)size > (uint8_t)limit) size = limit;
    const uint8_t *px = ROLE_PX[(int)size];
    my_font_14 = *raw(px[0]);
    my_font_18 = *raw(px[1]);
    my_font_24 = *raw(px[2]);
    my_font_36 = *raw(px[3]);
    s_current = size;
    return size;
}

TextSize current() { return s_current; }

}  // namespace MyFonts
