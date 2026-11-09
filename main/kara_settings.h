#pragma once

#include <stddef.h>
#include <stdint.h>

#include <string>
#include <vector>

namespace KaraSettings {

constexpr size_t MAX_FILE_SIZE = 8192;
constexpr size_t MAX_INSTRUMENT_MAPS = 64;

struct InstrumentMap {
    uint8_t channel = 0;         // MIDI channel, 0..15
    uint8_t sourceProgram = 0;   // GM program, 0..127
    uint8_t replacementProgram = 0;
};

struct Song {
    std::string soundfont;
    uint8_t midiVolume = 100;
    int8_t melodyChannel = -2;  // -2 = use MIDI auto-detection, -1 = no melody channel
    bool melodyMuted = false;
    uint8_t melodyGainPct = 100;
    uint8_t tempo = 100;
    int8_t transpose = 0;
    std::vector<InstrumentMap> instrumentMaps;
};

enum class Err : uint8_t { OK, TOO_BIG, INVALID };

bool validSoundfont(const std::string &name);
Err parse(const char *data, size_t size, Song &out);
Err serialize(const Song &song, std::string &out);

}  // namespace KaraSettings
