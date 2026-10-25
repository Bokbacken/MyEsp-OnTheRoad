// karaoke.cpp - see karaoke.h.
#include "karaoke.h"

#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#include <algorithm>

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
    if (s_open) {   // Without a sound bank the lyrics still run (silent mode)
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

bool findFont(char *path, size_t cap, char *name, size_t nameCap)
{
    snprintf(path, cap, "%s/default.sf2", DIR_SF2);
    struct stat st;
    if (stat(path, &st) == 0) {
        snprintf(name, nameCap, "default.sf2");
        return true;
    }
    DIR *d = opendir(DIR_SF2);
    if (!d) return false;
    bool found = false;
    static const char *const EXT[] = {".sf2", nullptr};
    while (dirent *e = readdir(d)) {
        if (e->d_name[0] == '.' || !hasExt(e->d_name, EXT)) continue;
        size_t n = strnlen(e->d_name, sizeof(e->d_name));
        if (n >= nameCap || strlen(DIR_SF2) + 1 + n >= cap) continue;   // Name too long for our buffers
        memcpy(name, e->d_name, n + 1);
        memcpy(path, DIR_SF2, strlen(DIR_SF2));
        path[strlen(DIR_SF2)] = '/';
        memcpy(path + strlen(DIR_SF2) + 1, name, n + 1);
        found = true;
        break;
    }
    closedir(d);
    return found;
}

void fontTask(void *)
{
    char path[128], name[48];
    Err err = Err::OK;
    {
        BoardSd::Guard g;
        if (!BoardSd::mounted()) err = Err::NO_SD;
        else if (!findFont(path, sizeof(path), name, sizeof(name))) err = Err::NO_SF2;
    }
    if (err == Err::OK) {
        // The synth object is only touched by the mixer while a song is open; the menu that starts
        // the load has closed every song, so no lock is needed during the slow read.
        const int64_t t0 = esp_log_timestamp();
        Sf::LoadErr le;
        {
            BoardSd::Guard g;
            le = s_synth.load(path, Synth::RATE);
        }
        switch (le) {
        case Sf::LoadErr::OK: err = Err::OK; break;
        case Sf::LoadErr::NO_FILE: err = Err::NO_SF2; break;
        case Sf::LoadErr::TOO_BIG: err = Err::SF2_TOO_BIG; break;
        case Sf::LoadErr::BAD_FILE: err = Err::SF2_BAD; break;
        case Sf::LoadErr::NO_MEMORY: err = Err::NO_MEMORY; break;
        }
        ESP_LOGI(TAG, "font %s: %s (%u bytes, %lld ms, PSRAM free %u)", name, Sf::loadErrName(le),
                 (unsigned)s_synth.fileSize(), (long long)(esp_log_timestamp() - t0),
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
        if (err == Err::OK) snprintf(s_fontName, sizeof(s_fontName), "%s", name);
    }
    s_fontErr = err;
    s_fontState = err == Err::OK ? FontState::READY : FontState::FAILED;
    vTaskDelete(nullptr);
}

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

void startLoadFont()
{
    if (s_fontState == FontState::READY || s_fontState == FontState::LOADING) return;
    if (!s_mux) s_mux = xSemaphoreCreateMutex();
    s_fontState = FontState::LOADING;
    if (xTaskCreatePinnedToCore(fontTask, "sf2load", 6144, nullptr, 3, nullptr, 1) != pdPASS) {
        s_fontErr = Err::NO_MEMORY;
        s_fontState = FontState::FAILED;
    }
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
    if (const Midi::RegEntry *r = s_reg.find(s_crc, s_size)) {
        if (r->melody != s_song.melody) s_melody = r->melody, s_melodyReg = true;
        tr = r->transpose;
        tempo = r->tempo;
    }
    s_player.load(&s_song);
    s_player.setTempo(tempo);
    s_synth.setTranspose(tr);
    s_player.seek(0, s_synth);
    applyMelodyMute();
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
    s_synth.reset();
    s_player.load(nullptr);
    s_song.clear();   // Frees the event/lyric buffers (PSRAM)
}

bool isOpen() { return s_open; }
const Midi::Song *song() { return s_open ? &s_song : nullptr; }

void play(bool on)
{
    Lock l;
    if (!s_open) return;
    if (on && s_player.finished()) s_player.seek(0, s_synth);
    s_player.play(on);
    if (!on) s_synth.allNotesOff();
}

bool playing() { return s_open && s_player.playing(); }

void restart()
{
    Lock l;
    if (s_open) s_player.seek(0, s_synth);
}

void seekRel(int ms)
{
    Lock l;
    if (!s_open) return;
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
        if (!s_open) return;
        s_synth.setTranspose(semis);
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
        if (!s_open || ch < -1 || ch > 15) return;
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
    if (s_open) applyMelodyMute();
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
    v.voices = s_synth.activeVoices();
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
    case Err::NO_MEMORY: return tr(Str::KARA_ERR_MEMORY);
    case Err::NO_FILE: return tr(Str::KARA_ERR_NO_FILE);
    case Err::FILE_TOO_BIG: return tr(Str::KARA_ERR_FILE_BIG);
    case Err::FILE_BAD: return tr(Str::KARA_ERR_FILE_BAD);
    }
    return "";
}

}  // namespace Karaoke
