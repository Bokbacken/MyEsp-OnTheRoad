#pragma once

#include <stdint.h>

namespace KaraokeLayout {

constexpr int32_t VOLUME_KNOB_WIDTH = 30;
constexpr int32_t VOLUME_KNOB_HEIGHT = 14;
constexpr int32_t VOLUME_TRACK_WIDTH = 12;

constexpr int32_t volumeKnobWidth(int32_t screenWidth)
{
    return screenWidth <= 480 ? VOLUME_KNOB_WIDTH * 2 / 3 : VOLUME_KNOB_WIDTH;
}

constexpr int32_t volumeTrackWidth(int32_t screenWidth)
{
    return screenWidth <= 480 ? VOLUME_TRACK_WIDTH * 2 / 3 : VOLUME_TRACK_WIDTH;
}

constexpr int32_t visibleLyricLines(int32_t screenWidth, int32_t availableHeight, int32_t lineHeight,
                                    int32_t gap)
{
    if (lineHeight <= 0) return 1;
    const int32_t rowGap = gap > 0 ? gap : 0;
    const int32_t count = (availableHeight + rowGap) / (lineHeight + rowGap);
    const int32_t maximum = screenWidth <= 480 ? 4 : 32;
    return count <= 0 ? 1 : count > maximum ? maximum : count;
}

struct VolumeRail {
    int32_t midiX, masterX, width, labelY, sliderY, height, lyricsX;
    int32_t lyricsWidth, seekWidth, timeX;
};

constexpr VolumeRail volumeRail(int32_t screenWidth, int32_t inset, int32_t gap, int32_t buttonHeight,
                                int32_t infoY, int32_t bottomY, int32_t lineHeight, int32_t minTouchWidth,
                                int32_t timeWidth)
{
    const bool compact = screenWidth <= 480;
    const int32_t normalWidth = minTouchWidth > 68 ? minTouchWidth : 68;
    const int32_t touchWidth = compact ? normalWidth * 2 / 3 : normalWidth;
    const int32_t edge = compact ? 0 : inset;
    const int32_t labelY = infoY + lineHeight + gap;
    const int32_t sliderY = labelY + lineHeight + (gap > VOLUME_KNOB_HEIGHT / 2 ? gap : VOLUME_KNOB_HEIGHT / 2);
    const int32_t lyricsX = edge + touchWidth + gap;
    const int32_t masterX = screenWidth - edge - touchWidth;
    const int32_t timeX = masterX - timeWidth - gap;
    return {
        edge,
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
