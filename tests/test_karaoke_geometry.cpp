#include <cassert>
#include <cstdio>

#include "karaoke_layout.h"

static void check(int32_t screenWidth, int32_t screenHeight, int32_t inset, int32_t gap, int32_t buttonHeight,
                  int32_t infoY, int32_t bottomY, int32_t lineHeight, int32_t minTouchWidth)
{
    constexpr int32_t timeWidth = 124;
    const auto rail = KaraokeLayout::volumeRail(screenWidth, inset, gap, buttonHeight, infoY, bottomY,
                                                lineHeight, minTouchWidth, timeWidth);
    const int32_t lyricsRight = inset + rail.lyricsWidth;
    const int32_t seekRight = 2 * inset + rail.seekWidth;

    assert(rail.width >= 52 && rail.height > 100);
    assert(KaraokeLayout::VOLUME_KNOB_WIDTH > KaraokeLayout::VOLUME_KNOB_HEIGHT);
    assert(rail.x + rail.width == screenWidth - inset);
    assert(lyricsRight + gap <= rail.x);
    assert(seekRight <= rail.timeX);
    assert(rail.timeX + timeWidth + gap <= rail.x);
    assert(rail.labelY >= infoY + lineHeight + gap);
    assert(rail.sliderY >= rail.labelY + lineHeight + KaraokeLayout::VOLUME_KNOB_HEIGHT / 2);
    assert(rail.sliderY + rail.height <= bottomY - gap - buttonHeight / 3);
    assert(rail.sliderY + rail.height + buttonHeight / 3 < bottomY);
    assert(bottomY < screenHeight);
}

int main()
{
    // ES3C35P 320x480 rotated to Karaoke's 480x320; 90° and 270° have identical logical geometry.
    check(480, 320, 10, 4, 44, 48, 266, 14, 52);
    check(480, 320, 10, 4, 44, 48, 266, 14, 52);
    // 480x800 profile rotated to the 800x480 logical Karaoke layout.
    check(800, 480, 12, 6, 52, 58, 416, 14, 78);
    std::puts("Karaoke volume rail: 480x320 and 800x480 bounds, touch width and no-overlap checks passed");
}
