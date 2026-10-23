// roam_policy.h - Pure roaming decisions for the STA (no Arduino deps, host-testable).
// The Wi-Fi manager scans in the background while connected; these helpers decide how
// often to scan and whether a stronger AP (same SSID, other BSSID) is worth a reconnect.
#pragma once
#include <stdint.h>
#include <string.h>

namespace Roam {

constexpr int GOOD_ENOUGH_DBM = -55;          // At or above: never roam
constexpr int HYSTERESIS_DB = 8;              // Candidate must be this much stronger...
constexpr int HYSTERESIS_WEAK_DB = 5;         // ...or this much when the link is really weak
constexpr int WEAK_DBM = -75;
constexpr uint32_t COOLDOWN_MS = 30000;       // Minimum time between two roams
constexpr uint32_t BSSID_PIN_MAX_AGE_MS = 30000;   // Scan results older than this are not used to pin a BSSID

struct Ap {
    uint8_t bssid[6];
    int8_t rssi;
    uint8_t channel;
};

// Background scan interval for the current link quality: weak links are rechecked often.
inline uint32_t scanIntervalMs(int rssi)
{
    if (rssi >= GOOD_ENOUGH_DBM) return 300000;   // 5 min: only to notice a big change
    if (rssi >= -65) return 60000;
    if (rssi >= -72) return 30000;
    return 15000;
}

inline bool sameBssid(const uint8_t *a, const uint8_t *b) { return memcmp(a, b, 6) == 0; }

// Index of the strongest AP, -1 when n <= 0.
inline int strongest(const Ap *aps, int n)
{
    int best = -1;
    for (int i = 0; i < n; i++)
        if (best < 0 || aps[i].rssi > aps[best].rssi) best = i;
    return best;
}

// True when moving from the current AP (curRssi) to a candidate (candRssi) is worth it.
inline bool shouldRoam(int curRssi, int candRssi)
{
    if (curRssi >= GOOD_ENOUGH_DBM) return false;
    int need = curRssi <= WEAK_DBM ? HYSTERESIS_WEAK_DB : HYSTERESIS_DB;
    return candRssi - curRssi >= need;
}

// Picks a roam target among scanned APs of the connected SSID. `cur` is the connected BSSID;
// its RSSI is taken from the same scan when present (comparable measurement), otherwise
// `linkRssi` is used. Returns the candidate index or -1.
inline int pickTarget(const Ap *aps, int n, const uint8_t *cur, int linkRssi)
{
    int curRssi = linkRssi;
    for (int i = 0; i < n; i++)
        if (sameBssid(aps[i].bssid, cur)) { curRssi = aps[i].rssi; break; }
    int best = -1;
    for (int i = 0; i < n; i++) {
        if (sameBssid(aps[i].bssid, cur)) continue;
        if (best < 0 || aps[i].rssi > aps[best].rssi) best = i;
    }
    if (best < 0 || !shouldRoam(curRssi, aps[best].rssi)) return -1;
    return best;
}

inline void formatBssid(const uint8_t *b, char *out /* >= 18 */)
{
    static const char hx[] = "0123456789ABCDEF";
    for (int i = 0; i < 6; i++) {
        out[i * 3] = hx[b[i] >> 4];
        out[i * 3 + 1] = hx[b[i] & 15];
        out[i * 3 + 2] = i < 5 ? ':' : '\0';
    }
}

}  // namespace Roam
