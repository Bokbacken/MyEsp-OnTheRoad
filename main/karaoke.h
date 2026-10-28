// karaoke.h - Karaoke engine: song files (.mid/.kar) from /sdcard/otr/karaoke, SoundFont from
// /sdcard/otr/sf2, the per-song register (karaoke.reg) and playback through the audio mixer's
// external source hook. The sequencer and the synth run inside the mixer task (sample-exact
// timing); UI calls take a short mutex.
#pragma once

#include <stdint.h>

#include <string>
#include <vector>

#include "kara_index.h"
#include "kara_stream.h"
#include "otr_midi.h"

namespace Karaoke {

constexpr const char *DIR_SONGS = "/sdcard/otr/karaoke";
constexpr const char *DIR_SF2 = "/sdcard/otr/sf2";
constexpr const char *REG_FILE = "/sdcard/otr/karaoke/karaoke.reg";
constexpr const char *IDX_FILE = "/sdcard/otr/karaoke/songs.idx";   // v0.13.7 song index
constexpr size_t MAX_SONGS = KaraIndex::MAX_SONGS;                  // v0.13.7: 2000 (was 500)

enum class Err : uint8_t {
    OK, NO_SD, NO_SF2, SF2_TOO_BIG, SF2_BAD, NO_MEMORY, NO_FILE, FILE_TOO_BIG, FILE_BAD, SF2_COMPRESSED,
    SONG_TOO_BIG,  // v0.13.2: the song needs more sample data than fits, even with one velocity layer
    BUSY           // v0.13.7: a sound bank is loading (web openByPath), try again shortly
};

struct SongFile {
    std::string path;       // Full path
    std::string title, artist;   // From the file name "Artist_Title.kar"
    uint32_t size;
    std::string dir;        // v0.13.7: folder relative to DIR_SONGS ("" = the root)
};

// Lists song files in DIR_SONGS and all its subfolders (any depth; sorted by artist, then title;
// max MAX_SONGS). Creates the folders when missing. Does not touch the index or the library.
Err scan(std::vector<SongFile> &out);

// v0.13.7 song library (shared by the device UI and the web song picker).
// load(): reads songs.idx; scans (and writes a new index) when the index is missing, corrupt or the
// SD free space (not counting /sdcard/otr/logs) or the mtime of DIR_SONGS differ from the index.
// rescan(): always scans and writes a new index. Both block (a scan of 2000 files takes seconds);
// rescanAsync() runs rescan() in its own task (scanning() is true meanwhile).
Err load();
bool loadIndex();               // Only the index part of load(): false = a scan is needed
Err rescan();
void rescanAsync();
bool scanning();
bool libraryLoaded();           // A load / scan has succeeded since boot
Err libraryError();             // Result of the last load / scan
uint32_t libraryGen();          // Changes whenever the library is replaced
// Runs fn with the library locked (keep it short; no SD access or LVGL waits inside)
template <class F> void withLibrary(F fn);
void libraryLock();
void libraryUnlock();
const KaraIndex::Library &libraryRef();   // Only valid between libraryLock/libraryUnlock
SongFile songAt(const KaraIndex::Library &lib, size_t i);
// Opens a song by path (relative to DIR_SONGS, or absolute below it) and starts playback as soon as
// the sound bank is ready (web song picker). Loads the song's sound bank when needed.
Err openByPath(const char *path);
uint32_t externalOpenGen();     // Increments on every successful openByPath (the UI follows it)
SongFile current();             // The open song ("" path when none)

// SoundFont: loading runs in its own task. v0.13.2: only the bank structure is read when a font is
// chosen; each opened song then loads just the samples it uses (LOADING with loadPct()), so banks
// larger than PSRAM work. Only one font lives in PSRAM.
// Font choice: the open song's register entry (sf2=...) wins, else the global default
// (Config::karaFont), else default.sf2, else the first valid .sf2 in DIR_SF2.
enum class FontState : uint8_t { NONE, LOADING, READY, FAILED };
struct FontFile {
    std::string name;       // File name in DIR_SF2
    uint32_t size;
    Err err;                // OK = playable; else why it is rejected (SF3, too big, not a SoundFont)
};
Err scanFonts(std::vector<FontFile> &out);   // Sorted by name; max 64 files
// Loads the font the current context wants (song font when a song is open, else the default).
// No-op when that request is already loaded/loading; retryFailed retries a failed request.
// While a song is open the reload pauses playback; the song keeps its position.
void ensureFont(bool retryFailed = false);
void startLoadFont();           // = ensureFont(true) (kept for the menu)
bool fontWanted();              // The current context wants a different font than the loaded one
FontState fontState();
Err fontError();
const char *fontName();         // File name of the loaded font ("" if none)
const char *defaultFont();      // Config::karaFont ("" = automatic)
void setDefaultFont(const char *name);   // Saved in NVS; "" = automatic
const char *songFont();         // Per-song font of the open song ("" = use the default)
void setSongFont(const char *name);      // Saved in the register; "" = use the default
void unloadFont();              // Frees the PSRAM (leaving the karaoke menu)
int loadPct();                  // Progress of the running load (0..100)
struct FontInfo {
    uint32_t fileSize;          // Bank file
    uint32_t bytes;             // Sample data loaded for the open song
    uint16_t bankPresets, presets, regions;
    bool prepared, reduced;     // reduced: only the velocity layer near 100 fitted
};
FontInfo fontInfo();

// v0.13.2: send effects, saved in NVS (Config::karaRev*, karaCho*)
struct Effects {
    bool reverb, chorus;
    uint8_t reverbLevel, chorusLevel;   // 0..100
};
Effects effects();
void setEffects(const Effects &e);

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
// -12..12, saved in the register. With a selectively loaded bank the song's samples are loaded
// again for the new keys (state LOADING, then the song continues); if they do not fit, the
// previous transpose stays and takeTransposeRejected() returns true once.
void setTranspose(int semis);
int transpose();
bool takeTransposeRejected();
void setMelody(int ch);         // -1..15, saved in the register (overrides the detection)
int melody();
bool melodyFromRegister();      // Current melody came from the register (user choice)
void setMelodyMuted(bool m);    // Sing the melody yourself
bool melodyMuted();
void setVolume(uint8_t v);      // 0..100; session changes update config RAM, caller persists after debounce
uint8_t volume();

// v0.13.6: countdown dots in pauses >= COUNTDOWN_GAP_MS (max COUNTDOWN_MAX dots), see
// KaraStream::countdownAt - shared by the device screen and the web stream
constexpr uint32_t COUNTDOWN_GAP_MS = KaraStream::COUNTDOWN_GAP_MS;
constexpr int COUNTDOWN_MAX = KaraStream::COUNTDOWN_MAX;

struct View {
    uint32_t posUs, lengthUs;
    int line, syl;
    int cdDots, cdLit;       // v0.13.6 countdown: dots (0 = none) and lit dots at posUs
    bool playing, finished;
    int voices, maxVoices;   // Active / limit (NVS kara_voices)
    uint8_t cpuPct;
};
View view();

template <class F> void withLibrary(F fn)
{
    libraryLock();
    fn(libraryRef(), libraryGen());
    libraryUnlock();
}

}  // namespace Karaoke
