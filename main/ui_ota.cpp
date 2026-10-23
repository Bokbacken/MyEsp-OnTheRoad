#include "ui_ota.h"

#include "my_sys.h"

#include "my_ota.h"
#include "my_theme.h"

namespace UiOta {

static lv_obj_t *s_overlay = nullptr;
static lv_obj_t *s_bar = nullptr;
static lv_obj_t *s_pct = nullptr;
static lv_obj_t *s_info = nullptr;
static MyOta::Phase s_shown = MyOta::Phase::IDLE;
static uint32_t s_hideAt = 0;

static void build()
{
    s_overlay = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(s_overlay);
    lv_obj_set_size(s_overlay, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(s_overlay, Theme::c(Theme::BG), 0);
    lv_obj_set_style_bg_opa(s_overlay, LV_OPA_90, 0);
    lv_obj_set_clickable(s_overlay, true);   // Swallow touches during the update
    lv_obj_set_scrollable(s_overlay, false);

    lv_obj_t *t = Theme::label(s_overlay, LV_SYMBOL_DOWNLOAD "  Uppdaterar firmware", &my_font_24, Theme::ACCENT);
    lv_obj_align(t, LV_ALIGN_CENTER, 0, -80);

    s_bar = lv_bar_create(s_overlay);
    lv_obj_set_size(s_bar, 260, 22);
    lv_obj_align(s_bar, LV_ALIGN_CENTER, 0, -20);
    lv_bar_set_range(s_bar, 0, 100);
    lv_obj_set_style_bg_color(s_bar, Theme::c(Theme::PANEL_RAISED), LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_bar, Theme::c(Theme::ACCENT), LV_PART_INDICATOR);

    s_pct = Theme::label(s_overlay, "0 %", &my_font_36);
    lv_obj_align(s_pct, LV_ALIGN_CENTER, 0, 40);

    s_info = Theme::label(s_overlay, "Stäng inte av enheten", &my_font_14, Theme::MUTED);
    lv_obj_set_width(s_info, 280);
    lv_obj_set_style_text_align(s_info, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(s_info, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_align(s_info, LV_ALIGN_CENTER, 0, 100);
}

static void tick(lv_timer_t *)
{
    MyOta::checkStale();
    MyOta::State st = MyOta::state();
    if (st.phase == MyOta::Phase::IDLE) return;
    if (!s_overlay) build();
    if (st.phase == MyOta::Phase::RUNNING) {
        s_hideAt = 0;
        int pct = st.total ? (int)((uint64_t)st.written * 100 / st.total) : 0;
        lv_bar_set_value(s_bar, pct, LV_ANIM_OFF);
        if (st.total) lv_label_set_text_fmt(s_pct, "%d %%", pct);
        else lv_label_set_text_fmt(s_pct, "%lu kB", (unsigned long)(st.written >> 10));
        lv_label_set_text(s_info, "Stäng inte av enheten");
    } else if (st.phase != s_shown) {
        if (st.phase == MyOta::Phase::DONE) {
            lv_bar_set_value(s_bar, 100, LV_ANIM_OFF);
            lv_label_set_text(s_pct, LV_SYMBOL_OK " Klart");
            lv_obj_set_style_text_color(s_pct, Theme::c(Theme::GREEN), 0);
            lv_label_set_text(s_info, "Startar om med den nya versionen...");
        } else {
            lv_label_set_text(s_pct, LV_SYMBOL_CLOSE " Misslyckades");
            lv_obj_set_style_text_color(s_pct, Theme::c(Theme::RED), 0);
            lv_label_set_text_fmt(s_info, "%s\nNuvarande firmware behålls.", st.error);
            s_hideAt = MySys::millis() + 6000;
        }
    }
    s_shown = st.phase;
    if (s_hideAt && (int32_t)(MySys::millis() - s_hideAt) >= 0) {   // Failure message shown long enough
        lv_obj_delete(s_overlay);
        s_overlay = nullptr;
        s_hideAt = 0;
    }
}

void begin()
{
    static bool started = false;
    if (started) return;
    started = true;
    lv_timer_create(tick, 250, nullptr);
}

}  // namespace UiOta
