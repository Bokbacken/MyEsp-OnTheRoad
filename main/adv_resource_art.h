#pragma once

#include <stddef.h>
#include <stdint.h>

namespace Adv {

struct ResourceArt {
    uint8_t bgra[32 * 32 * 4];
    bool ready = false;
};

// UI-task only. Uses LVGL's existing PNG decoder; input and output are caller-owned.
bool decodeResourcePng(const uint8_t *data, size_t size, ResourceArt &out);

template<typename Pixel>
void drawResource(const ResourceArt *art, int size, Pixel pixel)
{
    for (int y = 0; y < size; ++y) for (int x = 0; x < size; ++x) {
        const int u = x * 32 / size, v = y * 32 / size;
        if (art && art->ready) {
            const uint8_t *p = art->bgra + (v * 32 + u) * 4;
            if (p[3]) pixel(x, y, p[2], p[1], p[0], p[3]);
        } else {
            // A gold gathering bag, distinct from gems and ordinary crates.
            if ((v >= 10 && v <= 27 && u >= 7 && u <= 25) ||
                (v >= 4 && v <= 9 && u >= 11 && u <= 21))
                pixel(x, y, v == 10 || v == 11 ? 95 : 235, 155, 45, 255);
        }
    }
}

} // namespace Adv
