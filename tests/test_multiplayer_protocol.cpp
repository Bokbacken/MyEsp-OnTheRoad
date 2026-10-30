#include <cstdio>

#include "multiplayer_protocol.h"

static int s_pass = 0, s_fail = 0;
#define CHECK(c) do { if (c) s_pass++; else { s_fail++; printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); } } while (0)

int main()
{
    CHECK(MultiplayerProtocol::supports("1", "yatzy"));
    CHECK(MultiplayerProtocol::supports("1", "pig,yatzy,scc"));
    CHECK(!MultiplayerProtocol::supports("2", "yatzy"));
    CHECK(!MultiplayerProtocol::supports("1", "maxi-yatzy"));
    CHECK(!MultiplayerProtocol::supports(nullptr, "yatzy"));
    CHECK(!MultiplayerProtocol::supports("1", nullptr));

    CHECK(MultiplayerProtocol::joinableSeat(true, true, false));
    CHECK(!MultiplayerProtocol::joinableSeat(false, true, false));
    CHECK(!MultiplayerProtocol::joinableSeat(true, false, false));
    CHECK(!MultiplayerProtocol::joinableSeat(true, true, true));

    CHECK(MultiplayerProtocol::joinableGame(true, true, false, 1));
    CHECK(!MultiplayerProtocol::joinableGame(false, true, false, 1));
    CHECK(!MultiplayerProtocol::joinableGame(true, false, false, 1));
    CHECK(!MultiplayerProtocol::joinableGame(true, true, true, 1));
    CHECK(!MultiplayerProtocol::joinableGame(true, true, false, 0));

    printf("test_multiplayer_protocol: %d passed, %d failed\n", s_pass, s_fail);
    return s_fail ? 1 : 0;
}
