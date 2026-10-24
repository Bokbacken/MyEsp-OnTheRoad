// game_base.h - shared definitions for all dice games (see docs/GAME_ARCHITECTURE.md).
//
// No virtual interface yet: the UI and the web layer dispatch on GameType. Every game engine
// is pure C++ (no ESP-IDF / LVGL) so it can be unit tested on the host (tests/test_games.cpp).
#pragma once

#include <stdint.h>

enum class GameType : uint8_t { YATZY = 0, PIG = 1, FARKLE = 2, SCC = 3 };
enum class PigVariant : uint8_t { ONE_DIE = 0, TWO_DICE = 1 };

// Random source returning a die value 1..6 (shared by all engines). nullptr = rand().
typedef uint8_t (*YatzyDieRng)(void *ctx);

constexpr int GAME_NAME_CAP = 24;   // UTF-8 bytes incl. terminator (same as YATZY_NAME_CAP)

namespace GameBase {

// Stable insertion sort of `n` players by score (descending). Ties share the same place.
template <typename ScoreFn>
inline int rankPlayers(int n, int *order, int *places, ScoreFn score)
{
    for (int i = 0; i < n; i++) order[i] = i;
    for (int i = 1; i < n; i++) {
        int v = order[i], j = i;
        while (j > 0 && score(order[j - 1]) < score(v)) {
            order[j] = order[j - 1];
            j--;
        }
        order[j] = v;
    }
    for (int i = 0; i < n; i++) places[i] = (i > 0 && score(order[i]) == score(order[i - 1])) ? places[i - 1] : i + 1;
    return n;
}

// Clamps an RNG result to 1..6.
inline uint8_t clampDie(uint8_t v) { return (v >= 1 && v <= 6) ? v : (uint8_t)(v % 6 + 1); }

}  // namespace GameBase
