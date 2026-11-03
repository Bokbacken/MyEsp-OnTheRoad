#include "menu_art.h"

#include <array>
#include <cstddef>
#include <new>

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

constexpr void drawAdventure(Pixels &out, bool withCharacter = true)
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
    if (withCharacter) {
        circle(out, 17, 17, 2, GOLD);
        rect(out, 16, 19, 3, 5, PURPLE);
        rect(out, 15, 22, 2, 3, INK);
        rect(out, 18, 22, 2, 3, INK);
    }
}

constexpr void drawSudoku(Pixels &out, int frame)
{
    rect(out, 1, 1, 30, 30, INK);
    rect(out, 2, 2, 28, 28, DIE);
    for (int i = 1; i < 3; ++i) {
        rect(out, 1 + i * 10, 1, 1, 30, INK);
        rect(out, 1, 1 + i * 10, 30, 1, INK);
    }
    // A pencil mark in the first cell grows into a full digit.
    if (frame < 3) {
        rect(out, 3, 3, 1, 3, BLUE);
        rect(out, 6, 3, 2, 1, BLUE);
        rect(out, 7, 4, 1, 2, BLUE);
    } else rect(out, 5, 3, 2, 6, BLUE);
    if (frame >= 2) {
        rect(out, 15, 13, 5, 1, INK);
        rect(out, 19, 14, 1, 2, INK);
        rect(out, 15, 16, 5, 1, INK);
        rect(out, 15, 17, 1, 2, INK);
        rect(out, 15, 19, 5, 1, INK);
    }
    if (frame >= 5) {
        rect(out, 24, 23, 5, 1, GREEN);
        rect(out, 28, 24, 1, 5, GREEN);
        rect(out, 24, 26, 5, 1, GREEN);
        rect(out, 24, 29, 5, 1, GREEN);
    }
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
    case Icon::SUDOKU:
        drawSudoku(out, 7);
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
    render(Icon::SHIP_CAPTAIN_CREW), render(Icon::SUDOKU)
};

constexpr Pixels renderFrame(Icon icon, int frame)
{
    Pixels out{};
    if (icon == Icon::DICE) {
        Pixels source{};
        die(source, 7, 7, 18, frame % 6 + 1, GOLD);
        constexpr int cosines[] = {1024, 724, 0, -724, -1024, -724, 0, 724};
        constexpr int sines[] = {0, 724, 1024, 724, 0, -724, -1024, -724};
        for (int y = 0; y < SIDE; ++y)
            for (int x = 0; x < SIDE; ++x) {
                const int dx = 2 * x - 31, dy = 2 * y - 31;
                const int sx = (dx * cosines[frame] + dy * sines[frame] + 31 * 1024) / 2048;
                const int sy = (-dx * sines[frame] + dy * cosines[frame] + 31 * 1024) / 2048;
                if (sx >= 0 && sx < SIDE && sy >= 0 && sy < SIDE)
                    pixel(out, x, y, source[sy * SIDE + sx]);
            }
    } else if (icon == Icon::ADVENTURE) {
        drawAdventure(out, false);
        // Walk in front of the village; keep the houses stationary.
        const int x = 4 + (frame < 4 ? frame : 7 - frame) * 7;
        circle(out, x, 23, 2, GOLD);
        rect(out, x - 1, 25, 3, 4, PURPLE);
        rect(out, x - 2, 29, 2, frame % 2 ? 1 : 3, INK);
        rect(out, x + 1, 29, 2, frame % 2 ? 3 : 1, INK);
    } else if (icon == Icon::SUDOKU) {
        drawSudoku(out, frame);
    } else if (icon == Icon::KARAOKE) {
        Pixels source{};
        drawNotes(source);
        constexpr int bounce[] = {0, -1, -2, -1, 0, 1, 2, 1};
        for (int y = 0; y < SIDE; ++y)
            for (int x = 0; x < SIDE; ++x)
                if (source[y * SIDE + x])
                    pixel(out, x, y + bounce[(frame + x / 11 * 2) % ANIMATION_FRAMES],
                          source[y * SIDE + x]);
    }
    return out;
}

constexpr auto makeFrames(Icon icon)
{
    std::array<Pixels, ANIMATION_FRAMES> frames{};
    for (int i = 0; i < ANIMATION_FRAMES; ++i) frames[i] = renderFrame(icon, i);
    return frames;
}

constexpr std::array<std::array<Pixels, ANIMATION_FRAMES>, 4> FRAMES = {
    makeFrames(Icon::DICE), makeFrames(Icon::ADVENTURE), makeFrames(Icon::KARAOKE), makeFrames(Icon::SUDOKU)
};

struct HomeAnimation {
    lv_obj_t *screen;
    lv_obj_t *pictures[4];
    AnimationEnabled enabled;
    uint8_t frame = 0;
    bool moving = false;
};

void homeTick(lv_timer_t *timer)
{
    auto *state = static_cast<HomeAnimation *>(lv_timer_get_user_data(timer));
    if (lv_screen_active() != state->screen) return;
    const bool moving = state->enabled();
    if (!moving && !state->moving) return;
    state->moving = moving;
    for (int i = 0; i < 4; ++i) {
        if (!state->pictures[i]) continue;
        const Icon icon = i == 3 ? Icon::SUDOKU : static_cast<Icon>(i);
        lv_image_set_src(state->pictures[i], moving ? animationFrame(icon, state->frame) : image(icon));
    }
    if (moving) state->frame = (state->frame + 1) % ANIMATION_FRAMES;
    else state->frame = 0;
}

}  // namespace

const lv_image_dsc_t *animationFrame(Icon icon, uint8_t frame)
{
    if (icon != Icon::SUDOKU && icon > Icon::KARAOKE) return image(icon);
    const auto index = icon == Icon::SUDOKU ? 3 : static_cast<std::size_t>(icon);
    if (index >= FRAMES.size()) return image(icon);
    static const auto descriptors = [] {
        std::array<std::array<lv_image_dsc_t, ANIMATION_FRAMES>, 4> result{};
        for (std::size_t i = 0; i < result.size(); ++i)
            for (int f = 0; f < ANIMATION_FRAMES; ++f) {
                auto &d = result[i][f];
                d.header.magic = LV_IMAGE_HEADER_MAGIC;
                d.header.cf = LV_COLOR_FORMAT_ARGB8888;
                d.header.w = SIDE;
                d.header.h = SIDE;
                d.header.stride = SIDE * sizeof(uint32_t);
                d.data = reinterpret_cast<const uint8_t *>(FRAMES[i][f].data());
                d.data_size = sizeof(Pixels);
            }
        return result;
    }();
    return &descriptors[index][frame % ANIMATION_FRAMES];
}

bool animateHome(lv_obj_t *screen, lv_obj_t *dice, lv_obj_t *adventure, lv_obj_t *karaoke,
                 AnimationEnabled enabled, lv_obj_t *sudoku)
{
    auto *state = new (std::nothrow) HomeAnimation{screen, {dice, adventure, karaoke, sudoku}, enabled};
    if (!state) return false;
    lv_timer_t *timer = lv_timer_create(homeTick, 200, state);
    if (!timer) {
        delete state;
        return false;
    }
    lv_obj_add_event_cb(screen, [](lv_event_t *event) {
        if (lv_event_get_target(event) != lv_event_get_current_target(event)) return;
        auto *timer = static_cast<lv_timer_t *>(lv_event_get_user_data(event));
        if (lv_event_get_code(event) == LV_EVENT_SCREEN_UNLOAD_START) lv_timer_pause(timer);
        else if (lv_event_get_code(event) == LV_EVENT_SCREEN_LOADED) lv_timer_resume(timer);
        else if (lv_event_get_code(event) == LV_EVENT_DELETE) {
            delete static_cast<HomeAnimation *>(lv_timer_get_user_data(timer));
            lv_timer_delete(timer);
        }
    }, LV_EVENT_ALL, timer);
    return true;
}

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
