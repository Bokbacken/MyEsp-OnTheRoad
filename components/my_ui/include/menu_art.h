#pragma once

#include <stdint.h>
#include "lvgl.h"

namespace MenuArt {

enum class Icon : uint8_t {
    DICE,
    ADVENTURE,
    KARAOKE,
    YATZY,
    MIDI,
    MAXI,
    PIG,
    PIG_TWO,
    FARKLE,
    SHIP_CAPTAIN_CREW,
    COUNT
};

const lv_image_dsc_t *image(Icon icon);
constexpr uint8_t ANIMATION_FRAMES = 8;
const lv_image_dsc_t *animationFrame(Icon icon, uint8_t frame);
using AnimationEnabled = bool (*)();
bool animateHome(lv_obj_t *screen, lv_obj_t *dice, lv_obj_t *adventure, lv_obj_t *karaoke,
                 AnimationEnabled enabled);

}  // namespace MenuArt
