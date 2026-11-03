#pragma once

#include <stdint.h>

namespace KaraokeLayout {

constexpr int32_t VOLUME_KNOB_WIDTH = 30;
constexpr int32_t VOLUME_KNOB_HEIGHT = 14;
constexpr int32_t VOLUME_TRACK_WIDTH = 12;

constexpr int32_t visibleLyricLines(int32_t screenWidth, int32_t availableHeight, int32_t lineHeight,
                                    int32_t gap)
{
    if (screenWidth <= 480) return 4;
    if (lineHeight <= 0) return 1;
    const int32_t rowGap = gap > 0 ? gap : 0;
    const int32_t count = (availableHeight + rowGap) / (lineHeight + rowGap);
    return count <= 0 ? 1 : count > 32 ? 32 : count;
}

struct VolumeRail {
    int32_t midiX, masterX, width, labelY, sliderY, height, lyricsX;
    int32_t lyricsWidth, seekWidth, timeX;
};

constexpr VolumeRail volumeRail(int32_t screenWidth, int32_t inset, int32_t gap, int32_t buttonHeight,
                                int32_t infoY, int32_t bottomY, int32_t lineHeight, int32_t minTouchWidth,
                                int32_t timeWidth)
{
    const int32_t touchWidth = minTouchWidth > 68 ? minTouchWidth : 68;
    const int32_t labelY = infoY + lineHeight + gap;
    const int32_t sliderY = labelY + lineHeight + (gap > VOLUME_KNOB_HEIGHT / 2 ? gap : VOLUME_KNOB_HEIGHT / 2);
    const int32_t lyricsX = inset + touchWidth + gap;
    const int32_t masterX = screenWidth - inset - touchWidth;
    const int32_t timeX = masterX - timeWidth - gap;
    return {
        inset,
        masterX,
        touchWidth,
        labelY,
        sliderY,
        bottomY - gap - buttonHeight / 3 - sliderY,
        lyricsX,
        masterX - gap - lyricsX,
        timeX - lyricsX,
        timeX,
    };
}

}  // namespace KaraokeLayout
