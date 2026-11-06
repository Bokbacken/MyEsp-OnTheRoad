#pragma once

#include <stddef.h>
#include <stdint.h>

namespace TouchTest {

constexpr size_t MAX_SAMPLES = 512;
constexpr int KEYPAD_GAP = 3;

struct Sample {
    uint8_t grid;
    uint8_t target;
    uint8_t actual;
    uint8_t deadZone;
    uint16_t size;
    uint16_t areaH;
    uint16_t screenW;
    uint16_t screenH;
    int16_t originX;
    int16_t originY;
    int16_t expectedX;
    int16_t expectedY;
    int16_t x;
    int16_t y;
    int16_t releaseX;
    int16_t releaseY;
    bool landscape;
    bool cancelled;
    float maxMovePx = 0;
    uint32_t durationMs = 0;
    bool unregistered = false;
    bool fullScreen = false;
};

struct ZoneStats {
    unsigned samples = 0;
    unsigned correct = 0;
    unsigned rejected = 0;
    unsigned misassigned = 0;
    unsigned edgeSamples = 0;
};

struct ResultPoint {
    float x;
    float y;
};

inline bool resultPoint(const Sample &sample, int width, int height, ResultPoint &point)
{
    if (sample.unregistered || width <= 0 || height <= 0 ||
        sample.size == 0 || sample.areaH == 0) return false;
    if (sample.grid == 9) {
        point = {sample.releaseX * static_cast<float>(width) / sample.size,
                 sample.releaseY * static_cast<float>(height) / sample.areaH};
        return true;
    }
    if (sample.grid != 3 || sample.target >= 9) return false;
    const int sourceCellW = (sample.size - 4 * KEYPAD_GAP) / 5;
    const int sourceCellH = (sample.areaH - 2 * KEYPAD_GAP) / 3;
    if (sourceCellW <= 0 || sourceCellH <= 0) return false;
    const int col = sample.target % 3, row = sample.target / 3;
    const int sourceX = (col + 1) * (sourceCellW + KEYPAD_GAP);
    const int sourceY = row * (sourceCellH + KEYPAD_GAP);
    point = {(col + (sample.releaseX - sourceX) / static_cast<float>(sourceCellW)) * width / 3,
             (row + (sample.releaseY - sourceY) / static_cast<float>(sourceCellH)) * height / 3};
    return true;
}

inline int cellAtPoint(int x, int y, int size, int grid, int deadZone)
{
    if (grid <= 0 || size <= 0 || deadZone < 0) return -1;
    const int cell = size / grid;
    if (cell <= deadZone * 2 || x < 0 || y < 0 || x >= cell * grid || y >= cell * grid) return -1;
    const int column = x / cell, row = y / cell;
    if (x % cell < deadZone || x % cell >= cell - deadZone ||
        y % cell < deadZone || y % cell >= cell - deadZone) return -1;
    return row * grid + column;
}

inline int screenCellAtPoint(int x, int y, int width, int height)
{
    if (width < 9 || height < 9 || x < 0 || y < 0 || x >= width || y >= height) return -1;
    return ((y + 1) * 9 - 1) / height * 9 + ((x + 1) * 9 - 1) / width;
}

inline ZoneStats analyzeZone(const Sample *samples, size_t count, int grid, int size, int deadZone)
{
    ZoneStats stats;
    if (!samples || !grid || size <= 0) return stats;
    const int cell = size / grid;
    if (cell <= 0) return stats;
    for (size_t i = 0; i < count; ++i) {
        const Sample &sample = samples[i];
        if (sample.grid != grid || sample.target >= grid * grid || sample.cancelled ||
            sample.unregistered || sample.fullScreen) continue;
        ++stats.samples;
        const int sampleSize = sample.size ? sample.size : size;
        const int actual = cellAtPoint(sample.releaseX, sample.releaseY, sampleSize, grid, deadZone);
        if (actual < 0) ++stats.rejected;
        else if (actual == sample.target) ++stats.correct;
        else ++stats.misassigned;

        const int sampleCell = sampleSize / grid;
        if (sampleCell <= 0) continue;
        if (sample.releaseX < 0 || sample.releaseY < 0 ||
            sample.releaseX >= sampleSize || sample.releaseY >= sampleSize) continue;
        const int xInCell = sample.releaseX % sampleCell;
        const int yInCell = sample.releaseY % sampleCell;
        if (xInCell < 8 || xInCell >= sampleCell - 8 ||
            yInCell < 8 || yInCell >= sampleCell - 8)
            ++stats.edgeSamples;
    }
    return stats;
}

inline int recommendDeadZone(const Sample *samples, size_t count, int size)
{
    const ZoneStats baseline = analyzeZone(samples, count, 9, size, 0);
    if (baseline.samples < 81 || baseline.edgeSamples < 20) return -1;
    for (int deadZone = 0; deadZone <= 8; ++deadZone) {
        const ZoneStats stats = analyzeZone(samples, count, 9, size, deadZone);
        if (stats.misassigned == 0 && stats.correct * 100 >= baseline.samples * 90) return deadZone;
    }
    return -2;
}

}  // namespace TouchTest
