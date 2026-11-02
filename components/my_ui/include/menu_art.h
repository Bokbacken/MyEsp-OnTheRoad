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

}  // namespace MenuArt
