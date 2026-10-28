#pragma once

#include <stdint.h>

namespace KaraokeLayout {

constexpr int32_t VOLUME_KNOB_WIDTH = 30;
constexpr int32_t VOLUME_KNOB_HEIGHT = 14;

struct VolumeRail {
    int32_t x, width, labelY, sliderY, height;
    int32_t lyricsWidth, seekWidth, timeX;
};

constexpr VolumeRail volumeRail(int32_t screenWidth, int32_t inset, int32_t gap, int32_t buttonHeight,
                                int32_t infoY, int32_t bottomY, int32_t lineHeight, int32_t minTouchWidth,
                                int32_t timeWidth)
{
    const int32_t touchWidth = minTouchWidth > 52 ? minTouchWidth : 52;
    const int32_t labelY = infoY + lineHeight + gap;
    const int32_t sliderY = labelY + lineHeight + (gap > VOLUME_KNOB_HEIGHT / 2 ? gap : VOLUME_KNOB_HEIGHT / 2);
    return {
        screenWidth - inset - touchWidth,
        touchWidth,
        labelY,
        sliderY,
        bottomY - gap - buttonHeight / 3 - sliderY,
        screenWidth - 2 * inset - touchWidth - gap,
        screenWidth - 2 * inset - timeWidth - 2 * inset - touchWidth - gap,
        screenWidth - inset - timeWidth - touchWidth - gap,
    };
}

}  // namespace KaraokeLayout
