#include "menu_art.h"

#include <array>
#include <cstddef>

namespace MenuArt {
namespace {

constexpr int SIDE = 32;
constexpr uint32_t INK = 0xFF273449;
constexpr uint32_t DIE = 0xFFFFF4D8;
constexpr uint32_t BLUE = 0xFF3CA6D8;
constexpr uint32_t GREEN = 0xFF48B879;
constexpr uint32_t GOLD = 0xFFFFC64D;
constexpr uint32_t RED = 0xFFEB6A62;
constexpr uint32_t PURPLE = 0xFFB88CFA;
using Pixels = std::array<uint32_t, SIDE * SIDE>;

constexpr void pixel(Pixels &out, int x, int y, uint32_t color)
{
    if (x >= 0 && x < SIDE && y >= 0 && y < SIDE) out[y * SIDE + x] = color;
}

constexpr void rect(Pixels &out, int x, int y, int w, int h, uint32_t color)
{
    for (int iy = 0; iy < h; ++iy)
        for (int ix = 0; ix < w; ++ix) pixel(out, x + ix, y + iy, color);
}

constexpr void circle(Pixels &out, int cx, int cy, int r, uint32_t color)
{
    for (int y = -r; y <= r; ++y)
        for (int x = -r; x <= r; ++x)
            if (x * x + y * y <= r * r) pixel(out, cx + x, cy + y, color);
}

constexpr void die(Pixels &out, int x, int y, int size, int face, uint32_t fill = DIE)
{
    rect(out, x, y, size, size, INK);
    rect(out, x + 1, y + 1, size - 2, size - 2, fill);
    const int mid = size / 2;
    const int near = size / 4;
    const int far = size - 1 - near;
    const int pip = size >= 12 ? 2 : 1;
    if (face == 1 || face == 3 || face == 5) rect(out, x + mid, y + mid, pip, pip, INK);
    if (face >= 2) {
        rect(out, x + near, y + near, pip, pip, INK);
        rect(out, x + far - pip + 1, y + far - pip + 1, pip, pip, INK);
    }
    if (face >= 4) {
        rect(out, x + far - pip + 1, y + near, pip, pip, INK);
        rect(out, x + near, y + far - pip + 1, pip, pip, INK);
    }
    if (face == 6) {
        rect(out, x + near, y + mid - pip / 2, pip, pip, INK);
        rect(out, x + far - pip + 1, y + mid - pip / 2, pip, pip, INK);
    }
}

constexpr void diceCluster(Pixels &out)
{
    die(out, 0, 1, 10, 2, BLUE);
    die(out, 11, 0, 9, 4);
    die(out, 22, 2, 10, 5, GREEN);
    die(out, 3, 18, 10, 3);
    die(out, 18, 18, 12, 6, GOLD);
}

constexpr void drawNotes(Pixels &out)
{
    circle(out, 5, 24, 4, BLUE);
    circle(out, 18, 20, 4, GOLD);
    circle(out, 27, 25, 3, RED);
    rect(out, 8, 8, 2, 16, BLUE);
    rect(out, 8, 7, 9, 2, BLUE);
    rect(out, 15, 7, 2, 13, GOLD);
    rect(out, 17, 6, 9, 2, GOLD);
    rect(out, 24, 6, 2, 19, RED);
    rect(out, 24, 5, 6, 2, RED);
}

constexpr void drawAdventure(Pixels &out)
{
    rect(out, 1, 24, 30, 2, GREEN);
    rect(out, 4, 14, 11, 10, DIE);
    rect(out, 3, 13, 13, 2, INK);
    for (int row = 0; row < 7; ++row)
        for (int col = 0; col <= row; ++col)
            pixel(out, 9 - row + col * 2, 6 + row, RED);
    rect(out, 8, 18, 4, 6, BLUE);
    rect(out, 20, 16, 8, 8, BLUE);
    rect(out, 19, 14, 10, 2, INK);
    for (int row = 0; row < 5; ++row)
        for (int col = 0; col <= row; ++col)
            pixel(out, 23 - row + col * 2, 9 + row, GOLD);
    circle(out, 17, 17, 2, GOLD);
    rect(out, 16, 19, 3, 5, PURPLE);
    rect(out, 15, 22, 2, 3, INK);
    rect(out, 18, 22, 2, 3, INK);
}

constexpr Pixels render(Icon icon)
{
    Pixels out{};
    switch (icon) {
    case Icon::DICE:
        die(out, 7, 7, 18, 5, GOLD);
        break;
    case Icon::ADVENTURE:
        drawAdventure(out);
        break;
    case Icon::KARAOKE:
        drawNotes(out);
        break;
    case Icon::YATZY:
        diceCluster(out);
        break;
    case Icon::MIDI:
        die(out, 0, 1, 9, 2, BLUE);
        die(out, 11, 0, 9, 4);
        die(out, 22, 2, 9, 5, GREEN);
        die(out, 3, 18, 9, 3);
        die(out, 19, 18, 10, 6, GOLD);
        circle(out, 2, 30, 1, PURPLE);
        circle(out, 8, 30, 1, PURPLE);
        circle(out, 14, 30, 1, PURPLE);
        break;
    case Icon::MAXI:
        die(out, 0, 0, 8, 1, BLUE);
        die(out, 11, 0, 8, 2);
        die(out, 22, 0, 8, 3, GREEN);
        die(out, 0, 17, 8, 4);
        die(out, 11, 17, 8, 5, GOLD);
        die(out, 22, 17, 8, 6);
        break;
    case Icon::PIG:
        die(out, 6, 6, 20, 1, RED);
        break;
    case Icon::PIG_TWO:
        die(out, 1, 8, 15, 2, RED);
        die(out, 16, 8, 15, 5, GOLD);
        break;
    case Icon::FARKLE:
        diceCluster(out);
        break;
    case Icon::SHIP_CAPTAIN_CREW:
        die(out, 0, 1, 9, 6, BLUE);
        die(out, 11, 1, 9, 5);
        die(out, 22, 1, 9, 4, GREEN);
        die(out, 4, 20, 10, 3, GOLD);
        die(out, 18, 20, 10, 5, RED);
        break;
    case Icon::COUNT:
        break;
    }
    return out;
}

constexpr std::size_t ICON_COUNT = static_cast<std::size_t>(Icon::COUNT);
constexpr std::array<Pixels, ICON_COUNT> PIXELS = {
    render(Icon::DICE), render(Icon::ADVENTURE), render(Icon::KARAOKE),
    render(Icon::YATZY), render(Icon::MIDI), render(Icon::MAXI),
    render(Icon::PIG), render(Icon::PIG_TWO), render(Icon::FARKLE),
    render(Icon::SHIP_CAPTAIN_CREW)
};

}  // namespace

const lv_image_dsc_t *image(Icon icon)
{
    static lv_image_dsc_t descriptors[ICON_COUNT]{};
    static bool initialized = false;
    if (!initialized) {
        for (std::size_t i = 0; i < ICON_COUNT; ++i) {
            descriptors[i].header.magic = LV_IMAGE_HEADER_MAGIC;
            descriptors[i].header.cf = LV_COLOR_FORMAT_ARGB8888;
            descriptors[i].header.w = SIDE;
            descriptors[i].header.h = SIDE;
            descriptors[i].header.stride = SIDE * sizeof(uint32_t);
            descriptors[i].data = reinterpret_cast<const uint8_t *>(PIXELS[i].data());
            descriptors[i].data_size = sizeof(PIXELS[i]);
        }
        initialized = true;
    }
    const std::size_t index = static_cast<std::size_t>(icon);
    return index < ICON_COUNT ? &descriptors[index] : nullptr;
}

}  // namespace MenuArt
