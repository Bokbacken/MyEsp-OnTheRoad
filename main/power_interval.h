#pragma once

#include <stdint.h>

namespace PowerInterval {
constexpr uint32_t PERIOD_MS = 300000;
constexpr unsigned GAME_COUNT = 7; // Yatzy, Pig, Farkle, SCC, adventure, karaoke, idle/other
struct Sample {
    unsigned game;
    bool screenOn, wifiOn, audioOn, eco;
    uint8_t brightness, sfxVolume, musicVolume;
    uint16_t batteryMv; // 0 means unavailable
};
struct Totals {
    uint32_t elapsed = 0, games[GAME_COUNT]{};
    uint32_t screen = 0, wifi = 0, audio = 0, eco = 0;
    uint64_t brightness = 0, sfxVolume = 0, musicVolume = 0;
    uint64_t batterySum = 0;
    uint32_t batterySamples = 0;
    uint16_t batteryMin = UINT16_MAX, batteryMax = 0;

    void add(const Sample &s, uint32_t ms)
    {
        elapsed += ms;
        games[s.game < GAME_COUNT ? s.game : GAME_COUNT - 1] += ms;
        if (s.screenOn) screen += ms;
        if (s.wifiOn) wifi += ms;
        if (s.audioOn) audio += ms;
        if (s.eco) eco += ms;
        brightness += uint64_t(s.screenOn ? s.brightness : 0) * ms;
        sfxVolume += uint64_t(s.sfxVolume) * ms;
        musicVolume += uint64_t(s.musicVolume) * ms;
        if (s.batteryMv) {
            batterySum += s.batteryMv;
            ++batterySamples;
            if (s.batteryMv < batteryMin) batteryMin = s.batteryMv;
            if (s.batteryMv > batteryMax) batteryMax = s.batteryMv;
        }
    }
};
}
