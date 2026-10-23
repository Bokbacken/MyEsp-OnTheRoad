#include "ui_clock.h"

#include "my_sys.h"
#include <stdio.h>
#include <time.h>

#include "my_time.h"
#include "my_theme.h"

namespace UiClock {

static const char *const WDAY[7] = {"sön", "mån", "tis", "ons", "tor", "fre", "lör"};
static const char *const MON[12] = {"jan", "feb", "mar", "apr", "maj", "jun",
                                    "jul", "aug", "sep", "okt", "nov", "dec"};
static constexpr int YEAR_MIN = 2024, YEAR_MAX = 2040;

bool format(char *buf, size_t cap, bool withDate)
{
    struct tm t;
    if (!MyTime::local(t)) {
        snprintf(buf, cap, "--:--");
        return false;
    }
    bool sure = MyTime::certain();
    if (withDate)
        snprintf(buf, cap, "%02d:%02d%s  %s %d %s", t.tm_hour, t.tm_min, sure ? "" : "?", WDAY[t.tm_wday],
                 t.tm_mday, MON[t.tm_mon]);
    else
        snprintf(buf, cap, "%02d:%02d%s", t.tm_hour, t.tm_min, sure ? "" : "?");
    return sure;
}

static void labelTick(lv_timer_t *tm)
{
    lv_obj_t *l = (lv_obj_t *)lv_timer_get_user_data(tm);
    bool withDate = lv_obj_get_user_data(l) != nullptr;
    char buf[40];
    bool sure = format(buf, sizeof(buf), withDate);
    lv_label_set_text(l, buf);
    lv_obj_set_style_text_color(l, Theme::c(sure ? Theme::MUTED : Theme::ACCENT), 0);
}

void attachLabel(lv_obj_t *label, bool withDate)
{
    lv_obj_set_user_data(label, withDate ? (void *)1 : nullptr);
    lv_timer_t *t = lv_timer_create(labelTick, 1000, label);
    labelTick(t);
    lv_obj_add_event_cb(label, [](lv_event_t *e) { lv_timer_delete((lv_timer_t *)lv_event_get_user_data(e)); },
                        LV_EVENT_DELETE, t);
}

// ----------------------------------------------------------------------------------------
// Setter screen
// ----------------------------------------------------------------------------------------
static lv_obj_t *s_day, *s_mon, *s_year, *s_hour, *s_min;
static void (*s_back)() = nullptr;

static lv_obj_t *roller(lv_obj_t *parent, const char *opts, int sel, int32_t w, int32_t x, int32_t y)
{
    lv_obj_t *r = lv_roller_create(parent);
    lv_roller_set_options(r, opts, LV_ROLLER_MODE_NORMAL);
    lv_roller_set_visible_row_count(r, 3);
    lv_roller_set_selected(r, sel, LV_ANIM_OFF);
    lv_obj_set_width(r, w);
    lv_obj_set_pos(r, x, y);
    lv_obj_set_style_text_font(r, &my_font_24, 0);
    lv_obj_set_style_bg_color(r, Theme::c(Theme::PANEL), 0);
    lv_obj_set_style_text_color(r, Theme::c(Theme::MUTED), 0);
    lv_obj_set_style_border_color(r, Theme::c(Theme::LINE), 0);
    lv_obj_set_style_bg_color(r, Theme::c(Theme::ACCENT), LV_PART_SELECTED);
    lv_obj_set_style_text_color(r, Theme::c(Theme::BG), LV_PART_SELECTED);
    return r;
}

static char *numbers(char *buf, int from, int to, bool pad)
{
    char *p = buf;
    for (int i = from; i <= to; i++) p += sprintf(p, pad ? "%02d%s" : "%d%s", i, i < to ? "\n" : "");
    return buf;
}

static void save()
{
    struct tm t = {};
    t.tm_year = YEAR_MIN + (int)lv_roller_get_selected(s_year) - 1900;
    t.tm_mon = (int)lv_roller_get_selected(s_mon);
    t.tm_mday = (int)lv_roller_get_selected(s_day) + 1;
    t.tm_hour = (int)lv_roller_get_selected(s_hour);
    t.tm_min = (int)lv_roller_get_selected(s_min);
    t.tm_isdst = -1;   // Let the TZ rules decide (summer/winter time)
    static const int DAYS[12] = {31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    int y = t.tm_year + 1900;
    int maxDay = DAYS[t.tm_mon] - (t.tm_mon == 1 && !((y % 4 == 0 && y % 100 != 0) || y % 400 == 0));
    if (t.tm_mday > maxDay) t.tm_mday = maxDay;   // 31 feb -> 28/29 feb
    time_t e = mktime(&t);
    MyTime::setManual(e);
}

void showSetter(void (*back)())
{
    s_back = back;
    struct tm now;
    if (!MyTime::local(now)) {
        now = {};
        now.tm_year = 2026 - 1900;
        now.tm_mday = 1;
        now.tm_hour = 12;
    }
    lv_obj_t *scr = Theme::screen();
    Theme::header(scr, "Ställ klockan");

    lv_obj_t *info = Theme::label(scr, "", &my_font_14, Theme::MUTED);
    lv_label_set_text_fmt(info, "Källa nu: %s. Med internet sätts tiden\nautomatiskt via NTP.",
                          MyTime::sourceText(MyTime::source()));
    lv_obj_set_pos(info, 12, 54);

    static char buf[200];
    lv_obj_t *dl = Theme::label(scr, "Datum", &my_font_14, Theme::ACCENT);
    lv_obj_set_pos(dl, 12, 104);
    s_day = roller(scr, numbers(buf, 1, 31, false), now.tm_mday - 1, 76, 10, 126);
    s_mon = roller(scr, "jan\nfeb\nmar\napr\nmaj\njun\njul\naug\nsep\nokt\nnov\ndec", now.tm_mon, 96, 92, 126);
    int ysel = now.tm_year + 1900 - YEAR_MIN;
    if (ysel < 0) ysel = 0;
    if (ysel > YEAR_MAX - YEAR_MIN) ysel = YEAR_MAX - YEAR_MIN;
    s_year = roller(scr, numbers(buf, YEAR_MIN, YEAR_MAX, false), ysel, 118, 194, 126);

    lv_obj_t *tl = Theme::label(scr, "Tid", &my_font_14, Theme::ACCENT);
    lv_obj_set_pos(tl, 12, 252);
    s_hour = roller(scr, numbers(buf, 0, 23, true), now.tm_hour, 96, 64, 274);
    lv_obj_t *colon = Theme::label(scr, ":", &my_font_36);
    lv_obj_set_pos(colon, 154, 300);
    s_min = roller(scr, numbers(buf, 0, 59, true), now.tm_min, 96, 170, 274);

    lv_obj_t *cancel = Theme::button(scr, LV_SYMBOL_CLOSE " Avbryt", 146, 44, Theme::MUTED);
    lv_obj_set_pos(cancel, 8, 480 - 50);
    lv_obj_add_event_cb(cancel, [](lv_event_t *) { if (s_back) s_back(); }, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *ok = Theme::button(scr, LV_SYMBOL_SAVE " Spara", 152, 44, Theme::ACCENT, true);
    lv_obj_set_pos(ok, 160, 480 - 50);
    lv_obj_add_event_cb(ok, [](lv_event_t *) {
        save();
        if (s_back) s_back();
    }, LV_EVENT_CLICKED, nullptr);
    Theme::loadScreen(scr);
}

}  // namespace UiClock
