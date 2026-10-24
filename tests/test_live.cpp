// Host tests for GameLive web-seat release (v0.7.2).
// g++ -std=c++17 -Wall -Wextra -Imain -Itests/stub tests/test_live.cpp main/game_live.cpp main/game_yatzy.cpp
//     main/game_pig.cpp main/game_farkle.cpp main/game_scc.cpp -o tests/test_live && ./tests/test_live
#include <cstdio>

#include "game_live.h"

static int s_pass = 0, s_fail = 0;
#define CHECK(c)                                                         \
    do {                                                                 \
        if (c) s_pass++;                                                 \
        else { s_fail++; printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); } \
    } while (0)

int main()
{
    bool web[3] = {true, false, true};
    uint16_t pid[3] = {1, 2, 3};
    GameLive::setSeats(web, pid, 3);
    const uint32_t sess = GameLive::session();
    CHECK(GameLive::isWeb(0) && !GameLive::isWeb(1) && GameLive::isWeb(2));
    CHECK(GameLive::takeReleased() == 0);

    // Browser leaves seat 0: web flag cleared, same session, UI sees bit 0 once
    const uint32_t v = GameLive::version();
    GameLive::releaseWeb(0);
    CHECK(!GameLive::isWeb(0) && GameLive::isWeb(2));
    CHECK(GameLive::session() == sess);
    CHECK(GameLive::version() != v);
    CHECK(GameLive::takeReleased() == 1u);
    CHECK(GameLive::takeReleased() == 0);

    // Releasing a non-web seat still reports it (harmless), no version bump
    const uint32_t v2 = GameLive::version();
    GameLive::releaseWeb(1);
    CHECK(GameLive::version() == v2);
    CHECK(GameLive::takeReleased() == 2u);

    // Out of range is ignored
    GameLive::releaseWeb(-1);
    GameLive::releaseWeb(YATZY_MAX_PLAYERS);
    CHECK(GameLive::takeReleased() == 0);

    // Pending releases do not leak into a new game
    GameLive::releaseWeb(2);
    GameLive::setSeats(web, pid, 3);
    CHECK(GameLive::takeReleased() == 0);
    CHECK(GameLive::isWeb(2));
    CHECK(GameLive::session() != sess);

    // Commands are untouched by a release
    GameLive::Cmd c = {GameLive::CmdType::ROLL, 0, 0};
    CHECK(GameLive::pushCmd(c));
    GameLive::releaseWeb(0);
    GameLive::Cmd out;
    CHECK(GameLive::popCmd(out) && out.type == GameLive::CmdType::ROLL);

    printf("test_live: %d passed, %d failed\n", s_pass, s_fail);
    return s_fail ? 1 : 0;
}
