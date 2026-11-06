#pragma once

#include <stddef.h>
#include <stdint.h>

namespace TouchTest {

constexpr size_t MAX_SAMPLES = 512;

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
};

struct ZoneStats {
    unsigned samples = 0;
    unsigned correct = 0;
    unsigned rejected = 0;
    unsigned misassigned = 0;
    unsigned edgeSamples = 0;
};

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

inline ZoneStats analyzeZone(const Sample *samples, size_t count, int grid, int size, int deadZone)
{
    ZoneStats stats;
    if (!samples || !grid || size <= 0) return stats;
    const int cell = size / grid;
    if (cell <= 0) return stats;
    for (size_t i = 0; i < count; ++i) {
        const Sample &sample = samples[i];
        if (sample.grid != grid || sample.target >= grid * grid || sample.cancelled || sample.unregistered) continue;
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
