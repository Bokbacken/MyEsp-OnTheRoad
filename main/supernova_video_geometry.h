#pragma once

#include <stdint.h>

namespace SupernovaVideo {

struct Rect {
    uint16_t x, y, width, height;
};

inline Rect fit(uint16_t sourceWidth, uint16_t sourceHeight, uint16_t displayWidth, uint16_t displayHeight)
{
    if (!sourceWidth || !sourceHeight || displayWidth < 2 || displayHeight < 2) return {};
    uint32_t width = displayWidth;
    uint32_t height = uint32_t(sourceHeight) * width / sourceWidth;
    if (height > displayHeight) {
        height = displayHeight;
        width = uint32_t(sourceWidth) * height / sourceHeight;
    }
    width &= ~1U;
    height &= ~1U;
    if (!width || !height) return {};
    return {uint16_t((displayWidth - width) / 2), uint16_t((displayHeight - height) / 2),
            uint16_t(width), uint16_t(height)};
}

}  // namespace SupernovaVideo
