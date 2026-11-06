#include <cassert>
#include <initializer_list>

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
    TouchTest::ResultPoint point{};
    Sample board{};
    board.grid = 9;
    board.size = board.areaH = 315;
    board.releaseX = 17;
    board.releaseY = 30;
    assert(TouchTest::resultPoint(board, 315, 315, point));
    assert(point.x == 17 && point.y == 30);
    assert(TouchTest::resultPoint(board, 630, 630, point));
    assert(point.x == 34 && point.y == 60);
    board.unregistered = true;
    assert(!TouchTest::resultPoint(board, 315, 315, point));
    Sample key{};
    key.grid = 3;
    key.size = 312; // Five 60 px keys and four 3 px gaps.
    key.areaH = 114; // Three 36 px rows and two 3 px gaps.
    for (unsigned target = 0; target < 9; ++target) {
        key.target = static_cast<uint8_t>(target);
        key.releaseX = static_cast<int16_t>((target % 3 + 1) * 63 + 30);
        key.releaseY = static_cast<int16_t>(target / 3 * 39 + 18);
        assert(TouchTest::resultPoint(key, 300, 300, point));
        assert(point.x == (target % 3) * 100 + 50);
        assert(point.y == (target / 3) * 100 + 50);
    }
    for (int width : {320, 480, 800}) {
        for (int height : {320, 480, 800}) {
            assert(TouchTest::screenCellAtPoint(0, 0, width, height) == 0);
            assert(TouchTest::screenCellAtPoint(width - 1, height - 1, width, height) == 80);
            assert(TouchTest::screenCellAtPoint(width, height - 1, width, height) == -1);
            for (int row = 0; row < 9; ++row)
                for (int col = 0; col < 9; ++col) {
                    assert(TouchTest::screenCellAtPoint(col * width / 9, row * height / 9, width, height) == row * 9 + col);
                    assert(TouchTest::screenCellAtPoint((col + 1) * width / 9 - 1, (row + 1) * height / 9 - 1,
                                                       width, height) == row * 9 + col);
                }
        }
    }
    return 0;
}
