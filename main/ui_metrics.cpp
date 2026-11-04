// ui_metrics.cpp - see header. Plain integer arithmetic, no allocation.
#include "ui_metrics.h"
#include "hw_profile.h"
#include "lvgl.h"
#include "my_fonts.h"

namespace UiMetrics {

int32_t screenW()
{
    lv_display_t *d = lv_display_get_default();
    int32_t w = d ? lv_display_get_horizontal_resolution(d) : 0;
    return w > 0 ? w : HwProfile::active().width;
}

int32_t screenH()
{
    lv_display_t *d = lv_display_get_default();
    int32_t h = d ? lv_display_get_vertical_resolution(d) : 0;
    return h > 0 ? h : HwProfile::active().height;
}

static int32_t sc(int32_t v) { return MyFonts::uiPixels(v); }

int32_t marginSmall() { return sc(4); }
int32_t marginNormal() { return sc(10); }
int32_t marginLarge() { return sc(14); }

int32_t buttonH()
{
    const int32_t minimum = my_font_18.line_height + sc(16);
    return sc(42) > minimum ? sc(42) : minimum;
}
int32_t buttonWMin() { return sc(52); }
int32_t buttonWMax() { return sc(300); }

int32_t cardSpacing() { return sc(4); }
int32_t cardMinH() { return sc(60); }

int32_t controlSpacing() { return sc(6); }
int32_t titleH() { return sc(44) > my_font_24.line_height + sc(12) ? sc(44) : my_font_24.line_height + sc(12); }
int32_t headerH() { return titleH() + sc(30); }
int32_t footerH() { return sc(26) > my_font_14.line_height + sc(8) ? sc(26) : my_font_14.line_height + sc(8); }

}  // namespace UiMetrics
