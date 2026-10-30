#pragma once

#include <string.h>

namespace MultiplayerProtocol {

struct RemoteActions {
    bool joined;
    bool active;
    bool finished;
    bool rolling;
    bool hasRolled;
    bool canRoll;
    int you;
    int current;
};

enum class RemoteStateDecision {
    IgnoreUntilJoined,
    ApplySnapshot,
    RequireFreshJoin
};

inline bool yourTurn(const RemoteActions &s)
{
    return s.joined && s.active && !s.finished && s.you >= 0 && s.you == s.current;
}

inline bool canRemoteRoll(const RemoteActions &s)
{
    return yourTurn(s) && !s.rolling && s.canRoll;
}

inline bool canRemoteHold(const RemoteActions &s)
{
    return yourTurn(s) && !s.rolling && s.hasRolled;
}

inline bool canRemoteChoose(const RemoteActions &s, bool categoryHasPotential)
{
    return canRemoteHold(s) && categoryHasPotential;
}

inline bool supports(const char *protocol, const char *games)
{
    if (!protocol || strcmp(protocol, "1") != 0 || !games) return false;
    const char *part = games;
    while (*part) {
        const char *end = strchr(part, ',');
        const size_t len = end ? (size_t)(end - part) : strlen(part);
        if (len == 5 && memcmp(part, "yatzy", len) == 0) return true;
        if (!end) break;
        part = end + 1;
    }
    return false;
}

inline bool joinableSeat(bool web, bool hasPin, bool connected)
{
    return web && hasPin && !connected;
}

inline bool joinableGame(bool active, bool yatzy, bool finished, int seats)
{
    return active && yatzy && !finished && seats > 0;
}

inline bool validSessionId(const char *id)
{
    if (!id || strlen(id) != 16) return false;
    for (int i = 0; i < 16; i++)
        if (!((id[i] >= '0' && id[i] <= '9') || (id[i] >= 'a' && id[i] <= 'f'))) return false;
    return true;
}

inline bool sameSession(const char *expected, const char *actual)
{
    return validSessionId(expected) && validSessionId(actual) && strcmp(expected, actual) == 0;
}

inline RemoteStateDecision remoteStateDecision(const char *expectedSession,
                                               const char *actualSession, bool joined)
{
    if (!sameSession(expectedSession, actualSession))
        return RemoteStateDecision::RequireFreshJoin;
    return joined ? RemoteStateDecision::ApplySnapshot : RemoteStateDecision::IgnoreUntilJoined;
}

}  // namespace MultiplayerProtocol
