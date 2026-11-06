#include <cassert>

#include "touch_test_math.h"

int main()
{
    using TouchTest::Sample;
    assert(TouchTest::cellAtPoint(15, 15, 270, 9, 0) == 0);
    assert(TouchTest::cellAtPoint(29, 15, 270, 9, 0) == 0);
    assert(TouchTest::cellAtPoint(29, 15, 270, 9, 1) == -1);
    assert(TouchTest::cellAtPoint(-1, 15, 270, 9, 0) == -1);
    assert(TouchTest::cellAtPoint(271, 15, 270, 9, 0) == -1);

    Sample samples[81]{};
    for (unsigned target = 0; target < 81; ++target) {
        const int col = target % 9, row = target / 9;
        const int x = col * 30 + 15, y = row * 30 + 15;
        samples[target] = {9, static_cast<uint8_t>(target), static_cast<uint8_t>(target), 0, 270, 270,
                           320, 480, 0, 0, static_cast<int16_t>(x), static_cast<int16_t>(y),
                           static_cast<int16_t>(x), static_cast<int16_t>(y),
                           static_cast<int16_t>(x), static_cast<int16_t>(y), false, false};
    }
    assert(TouchTest::analyzeZone(samples, 81, 9, 270, 0).edgeSamples == 0);
    for (unsigned i = 0; i < 20; ++i) {
        samples[i].releaseX = static_cast<int16_t>((i % 9) * 30 + 3);
    }
    samples[1].releaseX = 29;
    const auto noZone = TouchTest::analyzeZone(samples, 81, 9, 270, 0);
    assert(noZone.correct == 80 && noZone.misassigned == 1 && noZone.edgeSamples >= 20);
    const auto onePixel = TouchTest::analyzeZone(samples, 81, 9, 270, 1);
    assert(onePixel.correct == 80 && onePixel.rejected == 1 && onePixel.misassigned == 0);
    assert(TouchTest::recommendDeadZone(samples, 81, 270) == 1);
    assert(TouchTest::recommendDeadZone(samples, 80, 270) == -1);
    samples[0].unregistered = true;
    const auto reported = TouchTest::analyzeZone(samples, 81, 9, 270, 0);
    assert(reported.samples == 80 && reported.correct == 79 && reported.misassigned == 1);
    assert(TouchTest::recommendDeadZone(samples, 81, 270) == -1);
    return 0;
}
