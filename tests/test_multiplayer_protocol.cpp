#include <cstdio>

#include "multiplayer_protocol.h"

static int s_pass = 0, s_fail = 0;
#define CHECK(c) do { if (c) s_pass++; else { s_fail++; printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); } } while (0)

int main()
{
    MultiplayerProtocol::RemoteActions action = {true, true, false, false, true, true, 1, 1};
    CHECK(MultiplayerProtocol::yourTurn(action));
    CHECK(MultiplayerProtocol::canRemoteRoll(action));
    CHECK(MultiplayerProtocol::canRemoteHold(action));
    CHECK(MultiplayerProtocol::canRemoteChoose(action, true));
    CHECK(!MultiplayerProtocol::canRemoteChoose(action, false));
    action.current = 0;
    CHECK(!MultiplayerProtocol::yourTurn(action));
    CHECK(!MultiplayerProtocol::canRemoteRoll(action));
    action.current = 1;
    action.joined = false;
    CHECK(!MultiplayerProtocol::canRemoteHold(action));
    action.joined = true;
    action.rolling = true;
    CHECK(!MultiplayerProtocol::canRemoteHold(action));
    action.rolling = false;
    action.hasRolled = false;
    CHECK(!MultiplayerProtocol::canRemoteChoose(action, true));
    action.hasRolled = true;
    action.finished = true;
    CHECK(!MultiplayerProtocol::canRemoteRoll(action));

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

    CHECK(MultiplayerProtocol::validSessionId("4a5b6c7d8e9f0123"));
    CHECK(!MultiplayerProtocol::validSessionId(nullptr));
    CHECK(!MultiplayerProtocol::validSessionId(""));
    CHECK(!MultiplayerProtocol::validSessionId("4a5b6c7d8e9f012"));
    CHECK(!MultiplayerProtocol::validSessionId("4a5b6c7d8e9f012g"));
    CHECK(MultiplayerProtocol::sameSession("4a5b6c7d8e9f0123", "4a5b6c7d8e9f0123"));
    CHECK(!MultiplayerProtocol::sameSession("4a5b6c7d8e9f0123", "4a5b6c7d8e9f0124"));
    CHECK(!MultiplayerProtocol::sameSession("invalid", "4a5b6c7d8e9f0123"));
    CHECK(MultiplayerProtocol::remoteStateDecision("4a5b6c7d8e9f0123",
          "4a5b6c7d8e9f0123", false) == MultiplayerProtocol::RemoteStateDecision::IgnoreUntilJoined);
    CHECK(MultiplayerProtocol::remoteStateDecision("4a5b6c7d8e9f0123",
          "4a5b6c7d8e9f0123", true) == MultiplayerProtocol::RemoteStateDecision::ApplySnapshot);
    CHECK(MultiplayerProtocol::remoteStateDecision("4a5b6c7d8e9f0123",
          "4a5b6c7d8e9f0124", true) == MultiplayerProtocol::RemoteStateDecision::RequireFreshJoin);
    CHECK(MultiplayerProtocol::remoteStateDecision("4a5b6c7d8e9f0123", nullptr,
          true) == MultiplayerProtocol::RemoteStateDecision::RequireFreshJoin);

    printf("test_multiplayer_protocol: %d passed, %d failed\n", s_pass, s_fail);
    return s_fail ? 1 : 0;
}
