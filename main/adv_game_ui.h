// LVGL adapter for the adventure viewport and controls.
#pragma once
#include "adv_game_geometry.h"
#include "lvgl.h"

namespace AdvUi {

inline lv_obj_t *gameRoot(lv_obj_t *screen, const Game::Layout &layout) {
    auto *root = lv_obj_create(screen);
    lv_obj_remove_style_all(root);
    lv_obj_set_size(root, layout.width, layout.height);
    lv_obj_set_pos(root, 0, 0);
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
