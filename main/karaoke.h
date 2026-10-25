// karaoke.h - Karaoke engine: song files (.mid/.kar) from /sdcard/otr/karaoke, SoundFont from
// /sdcard/otr/sf2, the per-song register (karaoke.reg) and playback through the audio mixer's
// external source hook. The sequencer and the synth run inside the mixer task (sample-exact
// timing); UI calls take a short mutex.
#pragma once

#include <stdint.h>

#include <string>
#include <vector>

#include "otr_midi.h"

namespace Karaoke {

constexpr const char *DIR_SONGS = "/sdcard/otr/karaoke";
constexpr const char *DIR_SF2 = "/sdcard/otr/sf2";
constexpr const char *REG_FILE = "/sdcard/otr/karaoke/karaoke.reg";

enum class Err : uint8_t { OK, NO_SD, NO_SF2, SF2_TOO_BIG, SF2_BAD, NO_MEMORY, NO_FILE, FILE_TOO_BIG, FILE_BAD };

struct SongFile {
    std::string path;       // Full path
    std::string title, artist;   // From the file name "Artist_Title.kar"
    uint32_t size;
};

// Lists song files (sorted by artist, then title). Creates the folders when missing.
Err scan(std::vector<SongFile> &out);

// SoundFont: loading takes a few seconds, so it runs in its own task.
enum class FontState : uint8_t { NONE, LOADING, READY, FAILED };
void startLoadFont();           // No-op when READY or LOADING
FontState fontState();
Err fontError();
const char *fontName();         // File name of the loaded font ("" if none)
void unloadFont();              // Frees the PSRAM (leaving the karaoke menu)

Err open(const SongFile &f);    // Parses the file and applies its register entry; paused at 0
void close();                   // Stops playback and detaches from the mixer
bool isOpen();
const Midi::Song *song();       // Valid while open (immutable)
const char *errText(Err e);     // Swedish/English user text via the language table

// Playback
void play(bool on);
bool playing();
void restart();
void seekRel(int ms);
void setTempo(int pct);         // 50..150, saved in the register
int tempo();
void setTranspose(int semis);   // -12..12, saved in the register
int transpose();
void setMelody(int ch);         // -1..15, saved in the register (overrides the detection)
int melody();
bool melodyFromRegister();      // Current melody came from the register (user choice)
void setMelodyMuted(bool m);    // Sing the melody yourself
bool melodyMuted();
void setVolume(uint8_t v);      // 0..100 (kept for the session)
uint8_t volume();

struct View {
    uint32_t posUs, lengthUs;
    int line, syl;
    bool playing, finished;
    int voices;
    uint8_t cpuPct;
};
View view();

}  // namespace Karaoke
