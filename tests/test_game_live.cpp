#include <cassert>
#include <cstdio>

#include "game_live.h"

static uint8_t six(void *) { return 6; }

int main()
{
    YatzyGame game;
    game.addPlayer("ESP");
    game.addPlayer("Host");
    game.setRng(six, nullptr);
    game.start();
    const bool web[] = {true, false};
    GameLive::setSeats(web, nullptr, 2);
    const uint32_t session = GameLive::session();
    const YatzyGame before = game;
    assert(game.roll());
    GameLive::publish(before, true, true, false, 31);
    const uint32_t token = GameLive::offerRollResult(game, 0);
    assert(token != 0 && !GameLive::rollResultAcknowledged(token));

    GameLive::Snapshot snap{};
    GameLive::get(snap); // Public HTTP snapshot must not leak the result.
    assert(snap.rolling && !snap.game.hasRolled() && snap.game.die(0) == 0);
    assert(GameLive::getForSeat(snap, -1, session) == 0);
    assert(snap.rolling && snap.game.die(0) == 0);
    assert(GameLive::getForSeat(snap, 1, session) == 0);
    assert(snap.rolling && snap.game.die(0) == 0);
    assert(GameLive::getForSeat(snap, 0, session + 1) == 0);
    assert(snap.rolling && snap.game.die(0) == 0);
    assert(GameLive::getForSeat(snap, 0, session) == token);
    assert(!snap.rolling && snap.rollMask == 0 && snap.game.rollsUsed() == 1);
    for (int i = 0; i < 5; ++i) assert(snap.game.die(i) == 6);

    assert(!GameLive::acknowledgeRollResult(1, session, token));
    assert(!GameLive::acknowledgeRollResult(0, session + 1, token));
    assert(!GameLive::acknowledgeRollResult(0, session, token + 1));
    assert(!GameLive::acknowledgeRollResult(0, session, 0));
    assert(!GameLive::rollResultAcknowledged(token));
    assert(GameLive::acknowledgeRollResult(0, session, token));
    assert(GameLive::acknowledgeRollResult(0, session, token)); // Idempotent retry.
    assert(GameLive::rollResultAcknowledged(token));
    GameLive::get(snap);
    assert(snap.rolling && snap.game.die(0) == 0); // Only LVGL may release the host.
    GameLive::publish(game, true, false, false);
    GameLive::get(snap);
    assert(!snap.rolling && snap.game.die(0) == 6);
    assert(GameLive::getForSeat(snap, 0, session) == 0);
    assert(!GameLive::acknowledgeRollResult(0, session, token));

    GameLive::publish(before, true, true, false, 31);
    const uint32_t second = GameLive::offerRollResult(game, 0);
    assert(second && second != token);
    assert(!GameLive::acknowledgeRollResult(0, session, token));
    GameLive::clearRollResult(); // Disconnect/timeout releases without retaining a private result.
    assert(GameLive::getForSeat(snap, 0, session) == 0);
    assert(!GameLive::acknowledgeRollResult(0, session, second));

    assert(GameLive::offerRollResult(game, 1) == 0);
    assert(GameLive::offerRollResult(game, -1) == 0);
    const uint32_t third = GameLive::offerRollResult(game, 0);
    assert(third);
    GameLive::setSeats(web, nullptr, 2);
    assert(!GameLive::acknowledgeRollResult(0, session, third));
    assert(GameLive::getForSeat(snap, 0, GameLive::session()) == 0);
    assert(GameLive::offerRollResult(game, 0));
    GameLive::setActive(false);
    assert(GameLive::getForSeat(snap, 0, GameLive::session()) == 0);
    assert(!GameLive::offerRollResult(game, 0));
    puts("GameLive private roll visibility, acknowledgements and session cleanup passed");
}
