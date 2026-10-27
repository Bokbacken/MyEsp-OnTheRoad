// ui_metrics.cpp - see header. Plain integer arithmetic, no allocation.
#include "ui_metrics.h"
#include "hw_profile.h"
#include "lvgl.h"

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

// Scale factor in percent from the short side (320 px = 100 %), limited so large panels get
// somewhat larger spacing without oversized controls.
static int32_t scalePct()
{
    int32_t w = screenW(), h = screenH();
    int32_t s = (w < h ? w : h) * 100 / 320;
    if (s < 100) s = 100;
    if (s > 150) s = 150;
    return s;
}

static int32_t sc(int32_t v) { return v * scalePct() / 100; }

int32_t marginSmall() { return sc(4); }
int32_t marginNormal() { return sc(10); }
int32_t marginLarge() { return sc(14); }

int32_t buttonH() { return sc(42); }
int32_t buttonWMin() { return sc(52); }
int32_t buttonWMax() { return sc(300); }

int32_t cardSpacing() { return sc(4); }
int32_t cardMinH() { return sc(60); }

int32_t headerH() { return sc(74); }
int32_t footerH() { return sc(26); }

}  // namespace UiMetrics
