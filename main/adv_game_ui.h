// LVGL adapter for the fixed-pixel adventure unit.
#pragma once
#include "adv_game_geometry.h"
#include "lvgl.h"
#include "ui_metrics.h"

namespace AdvUi {

inline lv_obj_t *gameRoot(lv_obj_t *screen) {
    auto *root = lv_obj_create(screen);
    lv_obj_remove_style_all(root);
    lv_obj_set_size(root, Game::WIDTH, Game::HEIGHT);
    const auto p = Game::origin(UiMetrics::screenW(), UiMetrics::screenH());
    lv_obj_set_pos(root, p.x, p.y);
    lv_obj_set_scrollable(root, false);
    lv_obj_set_clickable(root, false);
    return root;
}

inline Game::Rect gameBounds(lv_obj_t *obj) {
    lv_area_t a;
    lv_obj_get_coords(obj, &a);
    return {a.x1, a.y1, a.x2 - a.x1 + 1, a.y2 - a.y1 + 1};
}

}  // namespace AdvUi
