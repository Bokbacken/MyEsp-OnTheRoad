// net_list.h - ordered list of up to 50 saved Wi-Fi networks (pure logic, no Arduino).
//
// Order = priority. At boot the device scans and joins the FIRST saved network that is
// visible; if that fails it tries the next visible one, and so on.
// - A newly added network is inserted at the top (the network you just picked by hand is
//   most likely the one you want next time). When the list is full, the last entry drops.
// - Updating an existing network (new password) keeps its position.
// - The web page can reorder entries (move up / down) and delete them.
// Persisted as one NVS blob by board_config.cpp. Host-tested in tests/test_core.cpp.
#pragma once

#include <stdint.h>
#include <string.h>

constexpr int NET_LIST_MAX = 50;

struct SavedNet {
    char ssid[33];
    char pass[65];   // Empty for open networks
};

struct NetList {
    uint8_t count;
    SavedNet items[NET_LIST_MAX];

    void clear() { memset(this, 0, sizeof(*this)); }

    int find(const char *ssid) const
    {
        if (!ssid) return -1;
        for (int i = 0; i < count; i++)
            if (strcmp(items[i].ssid, ssid) == 0) return i;
        return -1;
    }

    // Adds or updates a network. Returns its index, or -1 for invalid input.
    int upsert(const char *ssid, const char *pass)
    {
        if (!ssid || !ssid[0] || strlen(ssid) > 32) return -1;
        if (!pass) pass = "";
        if (strlen(pass) > 64) return -1;
        int i = find(ssid);
        if (i >= 0) {
            memset(items[i].pass, 0, sizeof(items[i].pass));
            strcpy(items[i].pass, pass);
            return i;
        }
        int n = count < NET_LIST_MAX ? count : NET_LIST_MAX - 1;   // Full: drop the last one
        memmove(&items[1], &items[0], sizeof(SavedNet) * n);
        memset(&items[0], 0, sizeof(SavedNet));
        strcpy(items[0].ssid, ssid);
        strcpy(items[0].pass, pass);
        count = (uint8_t)(n + 1);
        return 0;
    }

    bool remove(int i)
    {
        if (i < 0 || i >= count) return false;
        memmove(&items[i], &items[i + 1], sizeof(SavedNet) * (count - i - 1));
        count--;
        memset(&items[count], 0, sizeof(SavedNet));
        return true;
    }

    // Moves entry i by delta positions (negative = higher priority). Clamped to the list.
    bool move(int i, int delta)
    {
        if (i < 0 || i >= count || delta == 0) return false;
        int j = i + delta;
        if (j < 0) j = 0;
        if (j >= count) j = count - 1;
        if (j == i) return false;
        SavedNet t = items[i];
        if (j < i) memmove(&items[j + 1], &items[j], sizeof(SavedNet) * (i - j));
        else memmove(&items[i], &items[i + 1], sizeof(SavedNet) * (j - i));
        items[j] = t;
        return true;
    }

    // First saved network with index >= start whose SSID is in `visible`. -1 if none.
    int firstVisible(const char *const *visible, int nVisible, int start = 0) const
    {
        for (int i = start < 0 ? 0 : start; i < count; i++)
            for (int v = 0; v < nVisible; v++)
                if (visible[v] && strcmp(items[i].ssid, visible[v]) == 0) return i;
        return -1;
    }
};
