#include "ui_arcade.h"

#include <algorithm>
#include <stdio.h>

#include "arcade_touch.h"
#include "board_display.h"
#include "board_touch.h"
#include "esp_log.h"
#include "lang.h"
#include "my_theme.h"
#include "ui_metrics.h"
#include "ui_yatzy.h"
#include "ui_screen_power.h"

namespace UiArcade {
namespace {

constexpr uint32_t TICK_MS = 33;
constexpr int GATE_COUNT = 3;
constexpr int BULLET_COUNT = 12;
constexpr int32_t GATE_SPEED = 58;
constexpr int32_t BULLET_SPEED = 230;
constexpr uint32_t FIRE_INTERVAL_MS = 320;
constexpr uint32_t HIT_GRACE_MS = 500;

struct Gate {
    lv_obj_t *top = nullptr;
    lv_obj_t *bottom = nullptr;
    lv_obj_t *target = nullptr;
    int32_t x = 0;
    int32_t gapCenter = 0;
    bool targetVisible = true;
    bool passed = false;
};

struct Bullet {
    lv_obj_t *obj = nullptr;
    int32_t x = 0;
    int32_t y = 0;
    bool active = false;
};

lv_obj_t *s_field = nullptr;
lv_obj_t *s_ship = nullptr;
lv_obj_t *s_status = nullptr;
lv_obj_t *s_heightControl = nullptr, *s_speedControl = nullptr;
lv_obj_t *s_heightMarker = nullptr, *s_speedMarker = nullptr;
lv_timer_t *s_timer = nullptr;
Gate s_gates[GATE_COUNT];
Bullet s_bullets[BULLET_COUNT];
int32_t s_fieldW = 0, s_fieldH = 0, s_shipX = 0, s_shipY = 0;
int32_t s_shipRadius = 12, s_gateWidth = 18, s_gapHeight = 72;
int32_t s_bulletWidth = 14;
uint32_t s_lastTick = 0, s_fireElapsed = 0, s_hitAt = 0;
uint32_t s_passed = 0, s_targetsHit = 0, s_impacts = 0;
bool s_hitGrace = false;
unsigned s_nextBullet = 0;
ArcadeTouch::Motion s_motion;
float s_targetY = 0;
bool s_controlsArmed = false;
uint32_t s_inputStartedAt = 0;

void setHidden(lv_obj_t *obj, bool hidden)
{
    lv_obj_set_hidden(obj, hidden);
}

void placeGate(Gate &gate)
{
    const int32_t gapTop = gate.gapCenter - s_gapHeight / 2;
    const int32_t gapBottom = gapTop + s_gapHeight;
    lv_obj_set_pos(gate.top, gate.x, 0);
    lv_obj_set_size(gate.top, s_gateWidth, std::max<int32_t>(1, gapTop));
    lv_obj_set_pos(gate.bottom, gate.x, gapBottom);
    lv_obj_set_size(gate.bottom, s_gateWidth, std::max<int32_t>(1, s_fieldH - gapBottom));
    lv_obj_set_pos(gate.target, gate.x + (s_gateWidth - 12) / 2, gate.gapCenter - 6);
    setHidden(gate.target, !gate.targetVisible);
}

void updateStatus()
{
    char text[96];
    snprintf(text, sizeof(text), tr(Str::ARCADE_STATUS_FMT),
             (unsigned)s_passed, (unsigned)s_targetsHit, (unsigned)s_impacts);
    lv_label_set_text(s_status, text);
}

float readControls()
{
    float speed = 0;
    lv_obj_set_y(s_speedMarker, (s_fieldH - 5) / 2);
    const Touch::Contacts contacts = Touch::contacts();
    if (UiScreenPower::inputBlocked()) {
        s_controlsArmed = false;
        return 0;
    }
    if (int32_t(contacts.sampledAt - s_inputStartedAt) < 0 ||
        lv_tick_elaps(contacts.sampledAt) > 100) return 0;
    if (!s_controlsArmed) {
        if (contacts.count == 0) s_controlsArmed = true;
        return 0;
    }
    lv_area_t left, right;
    lv_obj_get_coords(s_heightControl, &left);
    lv_obj_get_coords(s_speedControl, &right);
    bool haveHeight = false, haveSpeed = false;
    for (unsigned i = 0; i < contacts.count && i < 2; ++i) {
        lv_point_t point = {contacts.points[i].x, contacts.points[i].y};
        lv_display_rotate_point(lv_display_get_default(), &point);
        if (point.y < left.y1 || point.y > left.y2) continue;
        const int32_t y = point.y - left.y1;
        if (!haveHeight && point.x >= left.x1 && point.x <= left.x2) {
            const auto target = ArcadeTouch::clampToField(
                s_shipX, y, s_fieldW, s_fieldH, s_shipRadius + 2);
            s_targetY = float(target.y);
            haveHeight = true;
        } else if (!haveSpeed && point.x >= right.x1 && point.x <= right.x2) {
            speed = ArcadeTouch::throttle(y, s_fieldH);
            haveSpeed = true;
        }
    }
    lv_obj_set_y(s_heightMarker, int32_t(s_targetY) - 2);
    lv_obj_set_y(s_speedMarker, int32_t((1.0f - speed) * (s_fieldH - 5) / 2));
    return speed;
}

void resetCourse()
{
    s_passed = s_targetsHit = s_impacts = 0;
    s_fireElapsed = 0;
    s_hitGrace = false;
    s_nextBullet = 0;
    s_shipX = s_fieldW / 7;
    s_shipY = s_fieldH / 2;
    s_motion = {float(s_shipX), float(s_shipY), 0, 0};
    s_targetY = float(s_shipY);
    s_controlsArmed = false;
    s_inputStartedAt = lv_tick_get();
    lv_obj_set_y(s_heightMarker, s_shipY - 2);
    lv_obj_set_y(s_speedMarker, s_fieldH / 2 - 2);
    lv_obj_set_pos(s_ship, s_shipX - s_shipRadius, s_shipY - s_shipRadius);
    setHidden(s_ship, false);
    for (int i = 0; i < GATE_COUNT; ++i) {
        Gate &gate = s_gates[i];
        gate.x = s_fieldW + i * (s_fieldW / 2);
        gate.gapCenter = s_fieldH * (i == 0 ? 35 : (i == 1 ? 66 : 50)) / 100;
        gate.targetVisible = true;
        gate.passed = false;
        placeGate(gate);
    }
    for (Bullet &bullet : s_bullets) {
        bullet.active = false;
        setHidden(bullet.obj, true);
    }
    updateStatus();
}

void update(lv_timer_t *)
{
    if (!s_field || !s_ship) return;
    const uint32_t previousPassed = s_passed;
    const uint32_t previousHits = s_targetsHit;
    const uint32_t previousImpacts = s_impacts;
    uint32_t elapsed = lv_tick_elaps(s_lastTick);
    s_lastTick = lv_tick_get();
    elapsed = std::min<uint32_t>(elapsed, 66);
    const float speed = readControls();
    if (UiScreenPower::inputBlocked()) {
        s_motion.vx = s_motion.vy = 0;
        s_targetY = s_motion.y;
        return;
    }
    ArcadeTouch::moveShip(s_motion, s_targetY, speed, s_fieldW, s_fieldH,
                         s_shipRadius + 2, elapsed);
    s_shipX = int32_t(std::lround(s_motion.x));
    s_shipY = int32_t(std::lround(s_motion.y));
    if (s_hitGrace && lv_tick_elaps(s_hitAt) >= HIT_GRACE_MS) s_hitGrace = false;

    s_fireElapsed += elapsed;
    while (s_fireElapsed >= FIRE_INTERVAL_MS) {
        s_fireElapsed -= FIRE_INTERVAL_MS;
        Bullet &bullet = s_bullets[s_nextBullet++ % BULLET_COUNT];
        bullet.active = true;
        bullet.x = s_shipX + s_shipRadius;
        bullet.y = s_shipY;
        setHidden(bullet.obj, false);
    }

    const int32_t move = (GATE_SPEED * (int32_t)elapsed + 500) / 1000;
    for (int i = 0; i < GATE_COUNT; ++i) {
        Gate &gate = s_gates[i];
        gate.x -= move;
        if (gate.x + s_gateWidth < 0) {
            gate.x += GATE_COUNT * (s_fieldW / 2);
            gate.targetVisible = true;
            gate.passed = false;
        }
        if (!gate.passed && gate.x + s_gateWidth < s_shipX) {
            gate.passed = true;
            ++s_passed;
        }

        if (!s_hitGrace &&
            ArcadeTouch::circleHitsGate(s_shipX, s_shipY, s_shipRadius, gate.x,
                                       s_gateWidth, s_fieldH, gate.gapCenter, s_gapHeight)) {
            ++s_impacts;
            s_hitAt = lv_tick_get();
            s_hitGrace = true;
        }

        if (gate.targetVisible) {
            for (Bullet &bullet : s_bullets) {
                if (!bullet.active || bullet.x + s_bulletWidth < gate.x ||
                    bullet.x > gate.x + s_gateWidth) continue;
                if (bullet.y >= gate.gapCenter - 9 && bullet.y <= gate.gapCenter + 9) {
                    gate.targetVisible = false;
                    bullet.active = false;
                    setHidden(bullet.obj, true);
                    ++s_targetsHit;
                    break;
                }
            }
        }
        placeGate(gate);
    }

    for (Bullet &bullet : s_bullets) {
        if (!bullet.active) continue;
        bullet.x += (BULLET_SPEED * (int32_t)elapsed + 500) / 1000;
        if (bullet.x > s_fieldW) {
            bullet.active = false;
            setHidden(bullet.obj, true);
        } else {
            lv_obj_set_pos(bullet.obj, bullet.x, bullet.y - 2);
        }
    }

    lv_obj_set_pos(s_ship, s_shipX - s_shipRadius, s_shipY - s_shipRadius);
    setHidden(s_ship, s_hitGrace && (lv_tick_get() / 70) % 2 == 0);
    if (s_passed != previousPassed || s_targetsHit != previousHits || s_impacts != previousImpacts)
        updateStatus();
}

void closeCourse()
{
    if (!Display::setLandscape(false)) {
        ESP_LOGE("arcade", "could not restore portrait mode");
        return;
    }
    if (s_timer) {
        lv_timer_delete(s_timer);
        s_timer = nullptr;
    }
    Touch::setMultiTouch(false);
    showMenu();
}

void screenDeleted(lv_event_t *)
{
    if (s_timer) {
        lv_timer_delete(s_timer);
        s_timer = nullptr;
    }
    s_field = s_ship = s_status = nullptr;
    s_heightControl = s_speedControl = s_heightMarker = s_speedMarker = nullptr;
    Touch::setMultiTouch(false);
    for (Gate &gate : s_gates) gate = {};
    for (Bullet &bullet : s_bullets) bullet = {};
}

lv_obj_t *coloredRect(lv_obj_t *parent, uint32_t color, int32_t radius = 3)
{
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_remove_style_all(obj);
    lv_obj_set_style_bg_color(obj, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(obj, radius, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_scrollable(obj, false);
    lv_obj_set_clickable(obj, false);
    return obj;
}

int32_t buttonWidth(const char *text)
{
    lv_point_t size;
    lv_text_get_size(&size, text, &my_font_18, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    return std::max<int32_t>(UiMetrics::buttonWMin(), size.x + 2 * UiMetrics::marginNormal());
}

void showTouchTest()
{
    if (s_timer) return;
    if (!Display::setLandscape(true)) {
        ESP_LOGE("arcade", "could not enter landscape mode for the touch course");
        return;
    }
    lv_obj_t *scr = Theme::screen();
    Theme::header(scr, tr(Str::ARCADE_TOUCH_TEST), false);
    const int32_t inset = UiMetrics::marginNormal();
    const int32_t header = Theme::headerHeight();
    const int32_t footerH = UiMetrics::buttonH();
    const int32_t fieldY = header + UiMetrics::marginSmall();
    const int32_t fieldBottom = UiMetrics::screenH() - footerH - 2 * inset;
    const int32_t controlWidth = UiMetrics::buttonWMin();
    const int32_t fieldX = inset + controlWidth + UiMetrics::marginSmall();

    s_field = lv_obj_create(scr);
    lv_obj_remove_style_all(s_field);
    lv_obj_set_pos(s_field, fieldX, fieldY);
    s_fieldW = UiMetrics::screenW() - 2 * fieldX;
    s_fieldH = fieldBottom - fieldY;
    lv_obj_set_size(s_field, s_fieldW, s_fieldH);
    lv_obj_set_style_bg_color(s_field, lv_color_hex(0x071321), 0);
    lv_obj_set_style_bg_opa(s_field, LV_OPA_COVER, 0);
    lv_obj_set_scrollable(s_field, false);
    lv_obj_set_clickable(s_field, false);

    s_heightControl = coloredRect(scr, 0x19354B, 6);
    s_speedControl = coloredRect(scr, 0x19354B, 6);
    for (lv_obj_t *control : {s_heightControl, s_speedControl}) {
        lv_obj_set_size(control, controlWidth, s_fieldH);
        lv_obj_set_clickable(control, true);
    }
    lv_obj_set_pos(s_heightControl, inset, fieldY);
    lv_obj_set_pos(s_speedControl, UiMetrics::screenW() - inset - controlWidth, fieldY);
    lv_obj_t *neutral = coloredRect(s_speedControl, 0x3B5261, 0);
    lv_obj_set_pos(neutral, 0, s_fieldH * 2 / 5);
    lv_obj_set_size(neutral, controlWidth, s_fieldH / 5);
    auto controlLabel = [&](lv_obj_t *parent, const char *text, int32_t y) {
        lv_obj_t *label = Theme::label(parent, text, &my_font_14, Theme::LINE);
        lv_obj_set_width(label, controlWidth);
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_pos(label, 0, y);
        lv_obj_set_clickable(label, false);
        lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_DOTS);
    };
    controlLabel(s_heightControl, tr(Str::ARCADE_HEIGHT), 4);
    controlLabel(s_speedControl, tr(Str::ARCADE_SPEED), 4);
    controlLabel(s_speedControl, ">", s_fieldH / 5);
    controlLabel(s_speedControl, "0", s_fieldH / 2 - my_font_14.line_height / 2);
    controlLabel(s_speedControl, "<", s_fieldH * 4 / 5 - my_font_14.line_height);
    s_heightMarker = coloredRect(s_heightControl, 0x72DDF2, 0);
    s_speedMarker = coloredRect(s_speedControl, 0xF5C84C, 0);
    for (lv_obj_t *marker : {s_heightMarker, s_speedMarker})
        lv_obj_set_size(marker, controlWidth, 4);

    s_shipRadius = std::max<int32_t>(11, s_fieldH / 22);
    s_gateWidth = std::max<int32_t>(16, s_fieldW / 30);
    s_gapHeight = std::max<int32_t>(s_shipRadius * 3, s_fieldH / 3);
    s_bulletWidth = std::max<int32_t>(12, s_fieldW / 38);

    for (int i = 0; i < 22; ++i) {
        lv_obj_t *star = coloredRect(s_field, (i % 4) ? 0x7289A0 : 0xA4C8E8, LV_RADIUS_CIRCLE);
        const int32_t x = (i * 73 + 19) % s_fieldW;
        const int32_t y = (i * 47 + 13) % s_fieldH;
        const int32_t size = (i % 5 == 0) ? 3 : 2;
        lv_obj_set_pos(star, x, y);
        lv_obj_set_size(star, size, size);
    }

    for (Gate &gate : s_gates) {
        gate.top = coloredRect(s_field, 0x29435A, 4);
        gate.bottom = coloredRect(s_field, 0x29435A, 4);
        gate.target = coloredRect(s_field, 0xF5C84C, LV_RADIUS_CIRCLE);
        lv_obj_set_style_border_color(gate.top, lv_color_hex(0x4E7795), 0);
        lv_obj_set_style_border_color(gate.bottom, lv_color_hex(0x4E7795), 0);
        lv_obj_set_style_border_width(gate.top, 1, 0);
        lv_obj_set_style_border_width(gate.bottom, 1, 0);
        lv_obj_set_size(gate.target, 12, 12);
    }

    for (Bullet &bullet : s_bullets) {
        bullet.obj = coloredRect(s_field, 0x7DE8FF, LV_RADIUS_CIRCLE);
        lv_obj_set_size(bullet.obj, s_bulletWidth, 4);
        setHidden(bullet.obj, true);
    }

    s_ship = lv_label_create(s_field);
    lv_obj_set_style_text_font(s_ship, &my_font_24, 0);
    lv_obj_set_style_text_color(s_ship, lv_color_hex(0x72DDF2), 0);
    lv_label_set_text(s_ship, ">");
    lv_obj_set_size(s_ship, s_shipRadius * 2, s_shipRadius * 2);
    lv_obj_set_style_text_align(s_ship, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_clickable(s_ship, false);

    s_status = Theme::label(s_field, "", &my_font_14, Theme::LINE);
    lv_obj_set_pos(s_status, 8, 5);
    lv_obj_set_width(s_status, s_fieldW - 16);
    lv_obj_set_clickable(s_status, false);

    const int32_t backWidth = buttonWidth(tr(Str::BTN_BACK));
    const int32_t resetWidth = buttonWidth(tr(Str::ARCADE_RESET));
    const int32_t gap = UiMetrics::marginSmall();
    lv_obj_t *back = Theme::button(scr, tr(Str::BTN_BACK), backWidth,
                                   footerH, Theme::MUTED);
    lv_obj_set_pos(back, inset, UiMetrics::screenH() - footerH - inset);
    lv_obj_add_event_cb(back, [](lv_event_t *) { closeCourse(); }, LV_EVENT_CLICKED, nullptr);

    lv_obj_t *reset = Theme::button(scr, tr(Str::ARCADE_RESET), resetWidth,
                                    footerH, Theme::LINE);
    lv_obj_set_pos(reset, inset + backWidth + gap,
                   UiMetrics::screenH() - footerH - inset);
    lv_obj_add_event_cb(reset, [](lv_event_t *) { resetCourse(); }, LV_EVENT_CLICKED, nullptr);

    lv_obj_t *help = Theme::label(scr, tr(Str::ARCADE_TOUCH_HELP), &my_font_14, Theme::MUTED);
    const int32_t helpX = inset + backWidth + resetWidth + 2 * gap;
    lv_obj_set_pos(help, helpX, UiMetrics::screenH() - footerH - inset);
    lv_obj_set_width(help, UiMetrics::screenW() - helpX - inset);
    lv_label_set_long_mode(help, LV_LABEL_LONG_MODE_DOTS);

    resetCourse();
    s_lastTick = lv_tick_get();
    lv_obj_add_event_cb(scr, screenDeleted, LV_EVENT_DELETE, nullptr);
    s_timer = lv_timer_create(update, TICK_MS, nullptr);
    if (!s_timer) {
        ESP_LOGE("arcade", "could not allocate touch course timer");
        lv_obj_delete(scr);
        Display::setLandscape(false);
        return;
    }
    Touch::setMultiTouch(true);
    Theme::loadScreen(scr);
}

}  // namespace

void showMenu()
{
    if (Display::landscape() && !Display::setLandscape(false)) {
        ESP_LOGE("arcade", "could not restore portrait mode");
        return;
    }
    Theme::setStatusShareAvailable(false);
    lv_obj_t *scr = Theme::screen();
    Theme::header(scr, tr(Str::MENU_ARCADE));
    lv_obj_t *card = Theme::menuCard(scr, UiMetrics::screenW() - 2 * UiMetrics::marginNormal(),
                                     tr(Str::ARCADE_TOUCH_TEST), tr(Str::ARCADE_TOUCH_SUBTITLE),
                                     nullptr, LV_SYMBOL_PLAY, Theme::BLUE);
    lv_obj_set_pos(card, UiMetrics::marginNormal(), UiMetrics::headerH() + UiMetrics::marginNormal());
    lv_obj_add_event_cb(card, [](lv_event_t *) { showTouchTest(); }, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *back = Theme::button(scr, tr(Str::BTN_BACK), buttonWidth(tr(Str::BTN_BACK)),
                                   UiMetrics::buttonH(), Theme::MUTED);
    lv_obj_align(back, LV_ALIGN_BOTTOM_MID, 0, -UiMetrics::marginSmall());
    lv_obj_add_event_cb(back, [](lv_event_t *) { UiYatzy::showMenu(); }, LV_EVENT_CLICKED, nullptr);
    Theme::loadScreen(scr);
}

}  // namespace UiArcade
