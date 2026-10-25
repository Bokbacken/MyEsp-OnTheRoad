// karaoke.cpp - see karaoke.h.
#include "karaoke.h"

#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#include <algorithm>

#include "app_config.h"
#include "audio_synth.h"
#include "board_audio.h"
#include "board_sd.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lang.h"
#include "otr_synth.h"

static const char *TAG = "karaoke";

namespace Karaoke {

namespace {

SemaphoreHandle_t s_mux = nullptr;
Sf::Synth s_synth;
Midi::Song s_song;
Midi::Player s_player;
Midi::Registry s_reg;
bool s_regLoaded = false;
bool s_open = false;
uint32_t s_crc = 0, s_size = 0;
std::string s_fileName;
int s_melody = -1;
bool s_melodyReg = false, s_melodyMuted = false;
uint8_t s_volume = 70;

volatile FontState s_fontState = FontState::NONE;
volatile Err s_fontErr = Err::OK;
char s_fontName[48] = "";
std::string s_fontReq;          // Request (song or default font name, "" = automatic) of the last load
char s_reqName[64] = "";        // Request handed to the font task
std::string s_songSf2;          // Per-song font of the open song
int s_pendingTr = 0;            // Transpose of a song opened while a font was loading

struct Lock {
    Lock()
    {
        if (!s_mux) s_mux = xSemaphoreCreateMutex();
        xSemaphoreTake(s_mux, portMAX_DELAY);
    }
    ~Lock() { xSemaphoreGive(s_mux); }
};

bool hasExt(const char *name, const char *const *exts)
{
    const char *dot = strrchr(name, '.');
    if (!dot) return false;
    for (const char *const *e = exts; *e; e++)
        if (!strcasecmp(dot, *e)) return true;
    return false;
}

// Mixer task callback: advance the sequencer by one block, then render.
void renderCb(int32_t *acc, int n, int gainQ8, void *)
{
    if (xSemaphoreTake(s_mux, 0) != pdTRUE) return;   // UI is changing something: skip one block
    // Without a sound bank the lyrics still run (silent mode). While a new font loads, the song is
    // frozen (the font task owns the synth).
    if (s_open && s_fontState != FontState::LOADING) {
        s_player.advance((uint32_t)((uint64_t)n * 1000000u / Synth::RATE), s_synth);
        s_synth.render(acc, n, gainQ8);
    }
    xSemaphoreGive(s_mux);
}

void loadRegistry()
{
    if (s_regLoaded) return;
    s_regLoaded = true;
    BoardSd::Guard g;
    FILE *f = fopen(REG_FILE, "rb");
    if (!f) return;
    std::string text;
    char buf[512];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0 && text.size() < 256 * 1024) text.append(buf, n);
    fclose(f);
    s_reg.load(text);
    ESP_LOGI(TAG, "register: %u songs", (unsigned)s_reg.size());
}

// Updates the entry of the open song and returns the new file text (caller holds s_mux).
std::string updateRegistry()
{
    if (!s_open) return std::string();
    Midi::RegEntry e;
    e.crc = s_crc, e.size = s_size;
    e.melody = (int8_t)s_melody;
    e.transpose = (int8_t)s_synth.transpose();
    e.tempo = (uint8_t)s_player.tempo();
    e.sf2 = s_songSf2;
    e.name = s_fileName;
    s_reg.set(e);
    return s_reg.save();
}

// Written without holding s_mux so the mixer keeps playing during the SD write.
void writeRegistry(const std::string &text)
{
    if (text.empty()) return;
    BoardSd::Guard g;
    if (!BoardSd::mounted()) return;
    char tmp[64];
    snprintf(tmp, sizeof(tmp), "%s.tmp", REG_FILE);
    FILE *f = fopen(tmp, "wb");
    if (!f) return;
    const bool ok = fwrite(text.data(), 1, text.size(), f) == text.size();
    fclose(f);
    if (ok) {
        remove(REG_FILE);
        rename(tmp, REG_FILE);
    }
}

void applyMelodyMute()   // Caller holds s_mux
{
    for (int ch = 0; ch < 16; ch++) s_synth.setMuted(ch, s_melodyMuted && ch == s_melody);
}

Err mapLoadErr(Sf::LoadErr le)
{
    switch (le) {
    case Sf::LoadErr::OK: return Err::OK;
    case Sf::LoadErr::NO_FILE: return Err::NO_SF2;
    case Sf::LoadErr::TOO_BIG: return Err::SF2_TOO_BIG;
    case Sf::LoadErr::BAD_FILE: return Err::SF2_BAD;
    case Sf::LoadErr::NO_MEMORY: return Err::NO_MEMORY;
    case Sf::LoadErr::COMPRESSED: return Err::SF2_COMPRESSED;
    }
    return Err::SF2_BAD;
}

bool fontPath(char *path, size_t cap, const char *name)
{
    const size_t n = strlen(name), d = strlen(DIR_SF2);
    if (!n || d + 1 + n >= cap || strchr(name, '/')) return false;
    memcpy(path, DIR_SF2, d);
    path[d] = '/';
    memcpy(path + d + 1, name, n + 1);
    return true;
}

bool exists(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0;
}

// Resolves a request to a file (caller holds the SD guard). An explicitly chosen file that exists
// is used as is (so a bad file gives a clear error); a missing one falls back to the automatic
// chain: global default -> default.sf2 -> first .sf2 that passes the probe.
bool findFont(const char *req, char *path, size_t cap, char *name, size_t nameCap)
{
    const char *cands[] = {req, Config::get().karaFont, "default.sf2"};
    for (const char *c : cands) {
        if (!c || !*c || strlen(c) >= nameCap) continue;
        if (fontPath(path, cap, c) && exists(path)) {
            snprintf(name, nameCap, "%s", c);
            return true;
        }
    }
    DIR *d = opendir(DIR_SF2);
    if (!d) return false;
    bool found = false;
    static const char *const EXT[] = {".sf2", nullptr};
    std::vector<std::string> names;
    while (dirent *e = readdir(d)) {
        if (e->d_name[0] == '.' || !hasExt(e->d_name, EXT)) continue;
        if (strnlen(e->d_name, sizeof(e->d_name)) < nameCap) names.emplace_back(e->d_name);
    }
    closedir(d);
    std::sort(names.begin(), names.end(),
              [](const std::string &a, const std::string &b) { return strcasecmp(a.c_str(), b.c_str()) < 0; });
    for (const std::string &n : names) {
        if (!fontPath(path, cap, n.c_str()) || Sf::probeFile(path) != Sf::LoadErr::OK) continue;
        snprintf(name, nameCap, "%s", n.c_str());
        found = true;
        break;
    }
    return found;
}

void fontTask(void *)
{
    char path[128], name[64];
    Err err = Err::OK;
    {
        BoardSd::Guard g;
        if (!BoardSd::mounted()) err = Err::NO_SD;
        else if (!findFont(s_reqName, path, sizeof(path), name, sizeof(name))) err = Err::NO_SF2;
    }
    {
        Lock l;   // Free the old font first: only one font in PSRAM
        s_synth.unload();
        s_fontName[0] = 0;
    }
    if (err == Err::OK) {
        // State is LOADING, so the mixer callback does not touch the synth during the slow read
        // and the UI calls that use the synth are no-ops.
        const int64_t t0 = esp_log_timestamp();
        Sf::LoadErr le;
        {
            BoardSd::Guard g;
            le = s_synth.load(path, Synth::RATE);
        }
        err = mapLoadErr(le);
        ESP_LOGI(TAG, "font %s: %s (%u bytes, %lld ms, PSRAM free %u)", name, Sf::loadErrName(le),
                 (unsigned)s_synth.fileSize(), (long long)(esp_log_timestamp() - t0),
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    }
    {
        Lock l;
        if (err == Err::OK) {
            const size_t n = strnlen(name, sizeof(s_fontName) - 1);
            memcpy(s_fontName, name, n);
            s_fontName[n] = 0;
        }
        if (s_open) {   // Re-apply the song state to the new synth (programs via seek, mute, transpose)
            s_synth.setTranspose(s_pendingTr);
            applyMelodyMute();
            s_player.seek(s_player.position(), s_synth);
        }
        s_fontErr = err;
        s_fontState = err == Err::OK ? FontState::READY : FontState::FAILED;
    }
    vTaskDelete(nullptr);
}

bool synthBusy() { return s_fontState == FontState::LOADING; }

std::string currentRequest() { return s_open && !s_songSf2.empty() ? s_songSf2 : std::string(Config::get().karaFont); }

}  // namespace

Err scan(std::vector<SongFile> &out)
{
    out.clear();
    BoardSd::Guard g;
    if (!BoardSd::mounted()) return Err::NO_SD;
    BoardSd::ensureDir("/sdcard/otr");
    BoardSd::ensureDir(DIR_SONGS);
    BoardSd::ensureDir(DIR_SF2);
    DIR *d = opendir(DIR_SONGS);
    if (!d) return Err::NO_SD;
    static const char *const EXT[] = {".mid", ".kar", ".midi", ".rmi", nullptr};
    while (dirent *e = readdir(d)) {
        if (e->d_name[0] == '.' || !hasExt(e->d_name, EXT)) continue;
        SongFile f;
        f.path = std::string(DIR_SONGS) + "/" + e->d_name;
        struct stat st;
        f.size = stat(f.path.c_str(), &st) == 0 ? (uint32_t)st.st_size : 0;
        Midi::nameFromFile(e->d_name, f.title, f.artist);
        out.push_back(std::move(f));
        if (out.size() >= 500) break;
    }
    closedir(d);
    std::sort(out.begin(), out.end(), [](const SongFile &a, const SongFile &b) {
        int c = strcasecmp(a.artist.c_str(), b.artist.c_str());
        return c ? c < 0 : strcasecmp(a.title.c_str(), b.title.c_str()) < 0;
    });
    return Err::OK;
}

void ensureFont(bool retryFailed)
{
    if (!s_mux) s_mux = xSemaphoreCreateMutex();
    const FontState st = s_fontState;
    if (st == FontState::LOADING) return;
    const std::string req = currentRequest();
    if (req == s_fontReq && (st == FontState::READY || (st == FontState::FAILED && !retryFailed))) return;
    {
        Lock l;
        s_fontReq = req;
        snprintf(s_reqName, sizeof(s_reqName), "%.63s", req.c_str());
        if (s_open) {
            s_player.play(false);
            s_synth.allNotesOff();
        }
        s_fontState = FontState::LOADING;
    }
    if (xTaskCreatePinnedToCore(fontTask, "sf2load", 6144, nullptr, 3, nullptr, 1) != pdPASS) {
        s_fontErr = Err::NO_MEMORY;
        s_fontState = FontState::FAILED;
    }
}

void startLoadFont() { ensureFont(true); }

bool fontWanted()
{
    return s_fontState != FontState::LOADING && currentRequest() != s_fontReq;
}

Err scanFonts(std::vector<FontFile> &out)
{
    out.clear();
    BoardSd::Guard g;
    if (!BoardSd::mounted()) return Err::NO_SD;
    BoardSd::ensureDir(DIR_SF2);
    DIR *d = opendir(DIR_SF2);
    if (!d) return Err::NO_SD;
    static const char *const EXT[] = {".sf2", ".sf3", ".sfz", nullptr};
    while (dirent *e = readdir(d)) {
        if (e->d_name[0] == '.' || !hasExt(e->d_name, EXT)) continue;
        if (strnlen(e->d_name, sizeof(e->d_name)) >= sizeof(Config::get().karaFont)) continue;
        FontFile f;
        f.name = e->d_name;
        char path[128];
        size_t sz = 0;
        Sf::LoadErr le = Sf::LoadErr::NO_FILE;
        if (fontPath(path, sizeof(path), e->d_name)) le = Sf::probeFile(path, &sz);
        f.size = (uint32_t)sz;
        f.err = mapLoadErr(le);
        out.push_back(std::move(f));
        if (out.size() >= 64) break;
    }
    closedir(d);
    std::sort(out.begin(), out.end(),
              [](const FontFile &a, const FontFile &b) { return strcasecmp(a.name.c_str(), b.name.c_str()) < 0; });
    return Err::OK;
}

const char *defaultFont() { return Config::get().karaFont; }

void setDefaultFont(const char *name)
{
    AppConfig &c = Config::get();
    snprintf(c.karaFont, sizeof(c.karaFont), "%.47s", name ? name : "");
    Config::save();
}

const char *songFont() { return s_songSf2.c_str(); }

void setSongFont(const char *name)
{
    std::string text;
    {
        Lock l;
        if (!s_open) return;
        s_songSf2 = name ? name : "";
        if (s_songSf2.size() > 47) s_songSf2.resize(47);
        text = updateRegistry();
    }
    writeRegistry(text);
}

FontState fontState() { return s_fontState; }
Err fontError() { return s_fontErr; }
const char *fontName() { return s_fontName; }

void unloadFont()
{
    if (s_fontState == FontState::LOADING) return;
    close();
    Lock l;
    s_synth.unload();
    s_fontName[0] = 0;
    s_fontReq.clear();
    s_fontState = FontState::NONE;
}

Err open(const SongFile &sf)
{
    close();
    loadRegistry();
    std::vector<uint8_t> data;
    {
        BoardSd::Guard g;
        if (!BoardSd::mounted()) return Err::NO_SD;
        FILE *f = fopen(sf.path.c_str(), "rb");
        if (!f) return Err::NO_FILE;
        fseek(f, 0, SEEK_END);
        long sz = ftell(f);
        fseek(f, 0, SEEK_SET);
        if (sz <= 0) { fclose(f); return Err::FILE_BAD; }
        if ((size_t)sz > Midi::MAX_FILE) { fclose(f); return Err::FILE_TOO_BIG; }
        data.resize((size_t)sz);
        const bool ok = fread(data.data(), 1, data.size(), f) == data.size();
        fclose(f);
        if (!ok) return Err::FILE_BAD;
    }
    Lock l;
    Midi::Err e = Midi::parse(data.data(), data.size(), s_song);
    if (e == Midi::Err::TOO_BIG) return Err::FILE_TOO_BIG;
    if (e != Midi::Err::OK) {
        ESP_LOGW(TAG, "%s: %s", sf.path.c_str(), Midi::errName(e));
        s_song.clear();
        return Err::FILE_BAD;
    }
    s_crc = Midi::crc32(data.data(), data.size());
    s_size = (uint32_t)data.size();
    const size_t slash = sf.path.find_last_of('/');
    s_fileName = slash == std::string::npos ? sf.path : sf.path.substr(slash + 1);
    s_melody = s_song.melody;
    s_melodyReg = false;
    s_melodyMuted = false;
    int tr = 0, tempo = 100;
    s_songSf2.clear();
    if (const Midi::RegEntry *r = s_reg.find(s_crc, s_size)) {
        s_songSf2 = r->sf2;
        if (r->melody != s_song.melody) s_melody = r->melody, s_melodyReg = true;
        tr = r->transpose;
        tempo = r->tempo;
    }
    s_player.load(&s_song);
    s_player.setTempo(tempo);
    s_pendingTr = tr;
    if (!synthBusy()) {
        s_synth.setTranspose(tr);
        s_player.seek(0, s_synth);
        applyMelodyMute();
    }
    s_open = true;
    ESP_LOGI(TAG, "%s: %u events, %u lines, melody ch %d%s, crc %08x", s_fileName.c_str(),
             (unsigned)s_song.events.size(), (unsigned)s_song.lines.size(), s_melody + 1,
             s_melodyReg ? " (register)" : "", (unsigned)s_crc);
    Audio::setExternalVolume(s_volume);
    Audio::setExternal(&renderCb, nullptr);
    return Err::OK;
}

void close()
{
    Audio::setExternal(nullptr, nullptr);
    Lock l;
    if (!s_open) return;
    s_open = false;
    s_player.play(false);
    if (!synthBusy()) s_synth.reset();
    s_player.load(nullptr);
    s_song.clear();   // Frees the event/lyric buffers (PSRAM)
}

bool isOpen() { return s_open; }
const Midi::Song *song() { return s_open ? &s_song : nullptr; }

void play(bool on)
{
    Lock l;
    if (!s_open || synthBusy()) return;
    if (on && s_player.finished()) s_player.seek(0, s_synth);
    s_player.play(on);
    if (!on) s_synth.allNotesOff();
}

bool playing() { return s_open && s_player.playing(); }

void restart()
{
    Lock l;
    if (s_open && !synthBusy()) s_player.seek(0, s_synth);
}

void seekRel(int ms)
{
    Lock l;
    if (!s_open || synthBusy()) return;
    int64_t p = (int64_t)s_player.position() + (int64_t)ms * 1000;
    if (p < 0) p = 0;
    s_player.seek((uint32_t)p, s_synth);
}

void setTempo(int pct)
{
    std::string text;
    {
        Lock l;
        if (!s_open) return;
        s_player.setTempo(pct);
        text = updateRegistry();
    }
    writeRegistry(text);
}

int tempo() { return s_player.tempo(); }

void setTranspose(int semis)
{
    std::string text;
    {
        Lock l;
        if (!s_open || synthBusy()) return;
        s_synth.setTranspose(semis);
        s_pendingTr = s_synth.transpose();
        text = updateRegistry();
    }
    writeRegistry(text);
}

int transpose() { return s_synth.transpose(); }

void setMelody(int ch)
{
    std::string text;
    {
        Lock l;
        if (!s_open || synthBusy() || ch < -1 || ch > 15) return;
        s_melody = ch;
        s_melodyReg = ch != s_song.melody;
        applyMelodyMute();
        text = updateRegistry();
    }
    writeRegistry(text);
}

int melody() { return s_melody; }
bool melodyFromRegister() { return s_melodyReg; }

void setMelodyMuted(bool m)
{
    Lock l;
    s_melodyMuted = m;
    if (s_open && !synthBusy()) applyMelodyMute();
}

bool melodyMuted() { return s_melodyMuted; }

void setVolume(uint8_t v)
{
    s_volume = v > 100 ? 100 : v;
    Audio::setExternalVolume(s_volume);
}

uint8_t volume() { return s_volume; }

View view()
{
    View v{};
    Lock l;
    if (!s_open) return v;
    v.posUs = s_player.position();
    v.lengthUs = s_song.lengthUs;
    v.line = s_song.lineAt(v.posUs);
    v.syl = s_song.sylAt(v.posUs);
    v.playing = s_player.playing();
    v.finished = s_player.finished();
    v.voices = synthBusy() ? 0 : s_synth.activeVoices();
    v.cpuPct = Audio::status().externalLoad;
    return v;
}

const char *errText(Err e)
{
    switch (e) {
    case Err::OK: return "";
    case Err::NO_SD: return tr(Str::KARA_ERR_NO_SD);
    case Err::NO_SF2: return tr(Str::KARA_ERR_NO_SF2);
    case Err::SF2_TOO_BIG: return tr(Str::KARA_ERR_SF2_BIG);
    case Err::SF2_BAD: return tr(Str::KARA_ERR_SF2_BAD);
    case Err::SF2_COMPRESSED: return tr(Str::KARA_ERR_SF2_SF3);
    case Err::NO_MEMORY: return tr(Str::KARA_ERR_MEMORY);
    case Err::NO_FILE: return tr(Str::KARA_ERR_NO_FILE);
    case Err::FILE_TOO_BIG: return tr(Str::KARA_ERR_FILE_BIG);
    case Err::FILE_BAD: return tr(Str::KARA_ERR_FILE_BAD);
    }
    return "";
}

}  // namespace Karaoke
