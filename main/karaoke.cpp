// karaoke.cpp - see karaoke.h.
#include "karaoke.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#include <time.h>

#include <algorithm>

#include "app_config.h"
#include "audio_synth.h"
#include "board_audio.h"
#include "board_sd.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "kara_web.h"
#include "kara_lyrics.h"
#include "kara_overload.h"
#include "fs_upload.h"
#include "kara_settings.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lang.h"
#include "hw_profile.h"
#include "otr_synth.h"

static const char *TAG = "karaoke";

namespace Karaoke {

namespace {

void memoryMark(const char *point)
{
    ESP_LOGI(TAG, "%s: internal=%u PSRAM=%u largest=%u", point,
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM));
}

SemaphoreHandle_t s_mux = nullptr;
Sf::Synth s_synth;
Midi::Song s_song;
std::string s_plainLyrics;
Midi::Player s_player;
Midi::Registry s_reg;
bool s_regLoaded = false;
bool s_open = false;
uint32_t s_crc = 0, s_size = 0;
std::string s_fileName;
int s_melody = -1;
bool s_melodyReg = false, s_melodyMuted = false;
uint8_t s_songVolume = 100;
uint8_t s_melodyGain = 100;
std::vector<KaraSettings::InstrumentMap> s_instrumentMaps;
KaraSettings::Song s_savedSettings;
bool s_previewActive = false, s_previewPending = false, s_previewResume = false;
int s_previewChannel = -1, s_previewProgram = -1, s_previewKey = 60;
uint32_t s_previewFrames = 0, s_previewPosition = 0;

volatile FontState s_fontState = FontState::NONE;
volatile Err s_fontErr = Err::OK;
char s_fontName[48] = "";
std::string s_fontReq;          // Request (song or default font name, "" = automatic) of the last load
char s_reqName[64] = "";        // Request handed to the font task
std::string s_songSf2;          // Per-song font of the open song
int s_pendingTr = 0;            // Transpose of a song opened while a font was loading
// v0.13.2 selective loading: every open() gets a new generation; the font task prepares (loads the
// samples of) the newest one. failGen: generation whose prepare failed (not retried by itself).
uint32_t s_songGen = 0, s_prepGen = 0, s_failGen = 0;
volatile int s_loadPct = 0;
bool s_taskOpenBank = false;    // Task job: open the bank first (font change) or only prepare
// v0.13.4: the samples are loaded for the keys as transposed, so a new transpose prepares again.
// prevTr: the transpose to fall back to when the new one does not fit; resumePlay: the song was
// playing and continues after the reload; trRejected: told to the UI once.
bool s_trReload = false, s_resumePlay = false;
int s_prevTr = 0;
volatile bool s_trRejected = false;
volatile bool s_overloadStop = false;   // Playback stopped by the overload guard; told to the UI once
// Diagnostics (logged every 5 s while a song is open)
esp_timer_handle_t s_diagTimer = nullptr;
// v0.13.7 song library (own mutex: never held while the mixer needs s_mux)
KaraIndex::Library s_lib;
uint32_t s_libGen = 0;
volatile Err s_libErr = Err::OK;
volatile bool s_scanning = false, s_libLoaded = false;
SongFile s_cur;                 // The open song
uint32_t s_extGen = 0;          // openByPath counter (UI follows)
volatile bool s_autoPlay = false;   // openByPath: start playing when the sound bank is ready
constexpr const char *LOG_DIR = "/sdcard/otr/logs";   // Not counted in the index's free space
volatile int s_voicesPeak = 0;
uint32_t s_lastLateRuns = 0;
uint32_t s_lastDmaQueueOverflows = 0;

struct Lock {
    Lock()
    {
        if (!s_mux) s_mux = xSemaphoreCreateMutex();
        xSemaphoreTake(s_mux, portMAX_DELAY);
    }
    ~Lock() { xSemaphoreGive(s_mux); }
};

SemaphoreHandle_t registryMux()
{
    static SemaphoreHandle_t mux = xSemaphoreCreateMutex();
    configASSERT(mux);
    return mux;
}

struct RegistryLock {
    RegistryLock()
    {
        xSemaphoreTake(registryMux(), portMAX_DELAY);
    }
    ~RegistryLock() { xSemaphoreGive(registryMux()); }
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
        if (s_autoPlay) {   // v0.13.7: song opened from the web picker
            s_autoPlay = false;
            if (s_player.finished()) s_player.seek(0, s_synth);
            s_player.play(true);
        }
        if (!s_previewActive)
            s_player.advance((uint32_t)((uint64_t)n * 1000000u / Synth::RATE), s_synth);
        const int songGainQ8 = (int)((int64_t)gainQ8 * s_songVolume / 100);
        s_synth.render(acc, n, songGainQ8);
        if (s_previewActive) {
            if ((uint32_t)n >= s_previewFrames) {
                s_synth.midi((uint8_t)(0x80 | s_previewChannel), (uint8_t)s_previewKey, 0);
                s_player.seek(s_previewPosition, s_synth);
                if (s_previewResume) s_player.play(true);
                s_previewActive = s_previewResume = false;
                s_previewChannel = -1;
                s_previewProgram = -1;
                s_previewFrames = 0;
            } else {
                s_previewFrames -= (uint32_t)n;
            }
        }
        KaraWeb::onRender(s_song, s_player.position());   // v0.13.5: lyric events for the web stream
        const int v = s_synth.activeVoices();
        if (v > s_voicesPeak) s_voicesPeak = v;
    }
    xSemaphoreGive(s_mux);
}

Err loadRegistry()
{
    if (s_regLoaded) return Err::OK;
    BoardSd::Guard g;
    if (!BoardSd::mounted()) return Err::NO_SD;
    FILE *f = fopen(REG_FILE, "rb");
    if (!f) {
        if (errno != ENOENT) {
            ESP_LOGE(TAG, "Cannot read registry: %s", strerror(errno));
            return Err::FILE_BAD;
        }
        s_regLoaded = true;
        return Err::OK;
    }
    std::string text;
    char buf[512];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0 && text.size() <= 256 * 1024) text.append(buf, n);
    const bool ok = !ferror(f) && text.size() <= 256 * 1024;
    fclose(f);
    if (!ok) {
        ESP_LOGE(TAG, "Cannot read registry completely or registry too large");
        return Err::FILE_BAD;
    }
    s_reg.load(text);
    const unsigned legacy = (unsigned)s_reg.size();
    s_reg.clearSongSettings();
    s_regLoaded = true;
    ESP_LOGI(TAG, "register: retired %u legacy song settings; %u playlists",
             legacy, (unsigned)s_reg.playlists().size());
    return Err::OK;
}

std::string songSettingsPath(const std::string &songPath)
{
    return songPath + ".kcfg";
}

Err readSongSettings(const std::string &songPath, KaraSettings::Song &settings)
{
    settings = KaraSettings::Song{};
    const std::string path = songSettingsPath(songPath);
    BoardSd::Guard g;
    if (!BoardSd::mounted()) return Err::NO_SD;
    FILE *f = fopen(path.c_str(), "rb");
    if (!f) return errno == ENOENT ? Err::OK : Err::SETTINGS_BAD;
    std::string text;
    char buf[256];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0 && text.size() <= KaraSettings::MAX_FILE_SIZE)
        text.append(buf, n);
    const bool ok = !ferror(f) && text.size() <= KaraSettings::MAX_FILE_SIZE;
    fclose(f);
    if (!ok) return Err::SETTINGS_BAD;
    const KaraSettings::Err err = KaraSettings::parse(text.data(), text.size(), settings);
    return err == KaraSettings::Err::OK ? Err::OK : Err::SETTINGS_BAD;
}

bool writeSongSettings(const std::string &songPath, const std::string &text)
{
    if (text.empty() || text.size() > KaraSettings::MAX_FILE_SIZE) return false;
    const std::string path = songSettingsPath(songPath);
    const std::string tmp = path + ".tmp";
    const std::string backup = path + ".bak";
    if (path.size() >= 320 || tmp.size() >= 320 || backup.size() >= 320) return false;
    BoardSd::Guard g;
    if (!BoardSd::mounted()) return false;
    FILE *f = fopen(tmp.c_str(), "wb");
    if (!f) return false;
    bool ok = fwrite(text.data(), 1, text.size(), f) == text.size();
    if (fflush(f) != 0 || ferror(f)) ok = false;
    if (fclose(f) != 0) ok = false;
    if (!ok || FsUpload::install(tmp.c_str(), path.c_str(), backup.c_str(), true) !=
                   FsUpload::InstallResult::INSTALLED) {
        ESP_LOGE(TAG, "Cannot save song settings %s", path.c_str());
        FsUpload::discard(tmp.c_str());
        return false;
    }
    return true;
}

// Caller holds s_mux; includes a pending transpose while the samples are loading.
KaraSettings::Song currentSettings()
{
    KaraSettings::Song settings;
    settings.soundfont = s_songSf2;
    settings.midiVolume = s_songVolume;
    settings.melodyChannel = s_melodyReg ? (int8_t)s_melody : -2;
    settings.melodyMuted = s_melodyMuted;
    settings.melodyGainPct = s_melodyGain;
    settings.tempo = (uint8_t)s_player.tempo();
    settings.transpose = (int8_t)s_pendingTr;
    settings.instrumentMaps = s_instrumentMaps;
    return settings;
}

void applyMelodyGain()   // Caller holds s_mux
{
    for (int ch = 0; ch < 16; ch++)
        s_synth.setChannelGain(ch, ch == s_melody ? s_melodyGain : 100);
}

void applyMelodyMute();

void applyInstrumentMaps()   // Caller holds s_mux
{
    Sf::InstrumentMap maps[KaraSettings::MAX_INSTRUMENT_MAPS];
    const size_t count = std::min(s_instrumentMaps.size(), KaraSettings::MAX_INSTRUMENT_MAPS);
    for (size_t i = 0; i < count; i++) {
        maps[i] = {s_instrumentMaps[i].channel, s_instrumentMaps[i].sourceProgram,
                   s_instrumentMaps[i].replacementProgram};
    }
    s_synth.setInstrumentMaps(maps, count);
}

void applySongSettings(const KaraSettings::Song &settings, bool useAutoMelody)   // Caller holds s_mux
{
    s_songSf2 = settings.soundfont;
    s_songVolume = settings.midiVolume;
    s_melodyMuted = settings.melodyMuted;
    s_melodyGain = settings.melodyGainPct;
    s_melodyReg = settings.melodyChannel != -2;
    if (!useAutoMelody && settings.melodyChannel != -2) s_melody = settings.melodyChannel;
    s_player.setTempo(settings.tempo);
    s_pendingTr = settings.transpose;
    s_instrumentMaps = settings.instrumentMaps;
    applyInstrumentMaps();
    applyMelodyGain();
    applyMelodyMute();
}

// Written without holding s_mux so the mixer keeps playing during the SD write.
bool writeRegistry(const std::string &text)
{
    if (text.empty() || text.size() > 256 * 1024) {
        ESP_LOGE(TAG, "Registry exceeds storage limit");
        return false;
    }
    BoardSd::Guard g;
    if (!BoardSd::mounted() || !BoardSd::ensureDir(DIR_SONGS)) {
        ESP_LOGE(TAG, "Cannot save registry: SD directory unavailable");
        return false;
    }
    char tmp[64];
    snprintf(tmp, sizeof(tmp), "%s.tmp", REG_FILE);
    FILE *f = fopen(tmp, "wb");
    if (!f) {
        ESP_LOGE(TAG, "Cannot open registry temporary file");
        return false;
    }
    bool ok = fwrite(text.data(), 1, text.size(), f) == text.size();
    if (fflush(f) != 0 || ferror(f)) ok = false;
    if (fclose(f) != 0) ok = false;
    char backup[64];
    snprintf(backup, sizeof(backup), "%s.bak", REG_FILE);
    if (!ok || FsUpload::install(tmp, REG_FILE, backup, true) != FsUpload::InstallResult::INSTALLED) {
        ESP_LOGE(TAG, "Cannot save registry %s", REG_FILE);
        FsUpload::discard(tmp);
        return false;
    }
    return true;
}

void applyMelodyMute()   // Caller holds s_mux
{
    for (int ch = 0; ch < 16; ch++) s_synth.setMuted(ch, s_melodyMuted && ch == s_melody);
}

Sf::Effects cfgEffects()
{
    const AppConfig &c = Config::get();
    Sf::Effects e;
    e.reverb = c.karaRev != 0;
    e.reverbLevel = c.karaRevLvl;
    e.chorus = c.karaCho != 0;
    e.chorusLevel = c.karaChoLvl;
    return e;
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
    if (req && *req) {
        char requestedPath[160];
        if (!fontPath(requestedPath, sizeof(requestedPath), req) || !exists(requestedPath))
            ESP_LOGW(TAG, "requested SoundFont missing: %s; using fallback", req);
    }
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

// Caller holds s_mux: the open song still needs its samples (and has not failed already)
bool needPrepare() { return s_open && s_synth.bankOpen() && s_songGen != s_prepGen && s_songGen != s_failGen; }

void progressCb(void *, int pct) { s_loadPct = pct; }

uint8_t effectiveVoiceLimit(uint8_t requested, uint16_t capacity)
{
    uint8_t effective = 0;
    for (uint8_t option : KARA_VOICE_OPTIONS)
        if (option <= capacity && option <= requested) effective = option;
    if (effective) return effective;
    for (uint8_t option : KARA_VOICE_OPTIONS)
        if (option <= capacity) return option;
    return 1;
}

// Every 5 s while a song is open: render load (average/peak in % of a mixer block), late blocks and
// voices, to find the cause of dropouts on the device
void diagCb(void *)
{
    const Audio::ExternalStats st = Audio::takeExternalStats();
    int peak, limit;
    uint32_t steals;
    {
        Lock l;
        peak = s_voicesPeak;
        s_voicesPeak = 0;
        limit = s_synth.maxVoices();
        steals = s_synth.takeVoiceSteals();
    }
    const uint32_t newLateRuns = st.underruns - s_lastLateRuns;
    s_lastLateRuns = st.underruns;
    const uint32_t newDmaOverflows = st.dmaQueueOverflows - s_lastDmaQueueOverflows;
    s_lastDmaQueueOverflows = st.dmaQueueOverflows;
    if (!s_open || s_fontState == FontState::LOADING || !st.blocks) return;
    const Sf::Effects fx = cfgEffects();
    ESP_LOGI(TAG,
             "diag: load avg %u.%02u%% max %u.%02u%%, late-runs %u (+%u), DMA-q-overflows %u (+%u), "
             "voices peak %d/%d, steals %u, %s, "
             "reverb %s/%u chorus %s/%u, free int %u min %u PSRAM %u",
             (unsigned)(st.avgLoad100 / 100), (unsigned)(st.avgLoad100 % 100), (unsigned)(st.maxLoad100 / 100),
             (unsigned)(st.maxLoad100 % 100), (unsigned)st.underruns, (unsigned)newLateRuns,
             (unsigned)st.dmaQueueOverflows, (unsigned)newDmaOverflows, peak, limit,
             (unsigned)steals,
             s_player.playing() ? "playing" : "paused", fx.reverb ? "on" : "off", (unsigned)fx.reverbLevel,
             fx.chorus ? "on" : "off", (unsigned)fx.chorusLevel,
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
             (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    if (!KaraOverload::shouldStop(st.avgLoad100)) return;
    {
        Lock l;
        if (!s_open || !s_player.playing()) return;
        s_player.play(false);
        s_synth.allNotesOff();
        s_overloadStop = true;
    }
    ESP_LOGW(TAG, "overload: stopped (%s, %d voices, avg %u%%, +%u late-runs, +%u DMA-q-overflows)", s_fontName, limit,
             (unsigned)(st.avgLoad100 / 100), (unsigned)newLateRuns, (unsigned)newDmaOverflows);
}

void startDiag()
{
    if (!s_diagTimer) {
        const esp_timer_create_args_t a = {.callback = &diagCb, .arg = nullptr, .dispatch_method = ESP_TIMER_TASK,
                                           .name = "karadiag", .skip_unhandled_events = true};
        if (esp_timer_create(&a, &s_diagTimer) != ESP_OK) return;
    }
    const Audio::ExternalStats audio = Audio::takeExternalStats();   // Start a fresh period
    s_lastLateRuns = audio.underruns;
    s_lastDmaQueueOverflows = audio.dmaQueueOverflows;
    s_voicesPeak = 0;
    s_synth.takeVoiceSteals();
    if (!esp_timer_is_active(s_diagTimer)) esp_timer_start_periodic(s_diagTimer, 5000000);
}

void stopDiag()
{
    if (s_diagTimer && esp_timer_is_active(s_diagTimer)) esp_timer_stop(s_diagTimer);
}

void fontTask(void *)
{
    memoryMark("bank-task-start");
    const HwProfile::Info &hw = HwProfile::active();
    s_synth.setMemoryLimits(hw.soundFontMaxBytes, hw.psramReserveBytes);
    s_synth.setResampling(hw.resampleSoundFonts);
    char path[128], name[64];
    Err err = Err::OK;
    s_loadPct = 0;
    // The cached effect WAVs go first: their PSRAM counts for the song's samples (they reload on use)
    Audio::releaseClips();
    {
        Lock l;
        s_synth.setVoiceCapacity(hw.maxKaraokeVoices);
        s_synth.setMaxVoices(effectiveVoiceLimit(Config::get().karaVoices, hw.maxKaraokeVoices));
    }
    if (s_taskOpenBank) {
        {
            BoardSd::Guard g;
            if (!BoardSd::mounted()) err = Err::NO_SD;
            else if (!findFont(s_reqName, path, sizeof(path), name, sizeof(name))) err = Err::NO_SF2;
        }
        {
            Lock l;
            s_fontName[0] = 0;
            if (err != Err::OK) s_synth.unload();
        }
        if (err == Err::OK) {
            // State is LOADING, so the mixer callback does not touch the synth during the slow read
            // and the UI calls that use the synth are no-ops. Same file as before: the structure
            // (hydra) is kept and only the samples are reloaded.
            const int64_t t0 = esp_log_timestamp();
            Sf::LoadErr le;
            {
                BoardSd::Guard g;
                le = s_synth.open(path, Synth::RATE);
            }
            err = mapLoadErr(le);
            ESP_LOGI(TAG, "font %s: %s (%u bytes, %d presets, %lld ms, PSRAM free %u)", name, Sf::loadErrName(le),
                     (unsigned)s_synth.fileSize(), s_synth.presetCount(), (long long)(esp_log_timestamp() - t0),
                     (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
            Lock l;
            if (err == Err::OK) {
                const size_t n = strnlen(name, sizeof(s_fontName) - 1);
                memcpy(s_fontName, name, n);
                s_fontName[n] = 0;
                s_synth.setEffects(cfgEffects());
                s_prepGen = 0;   // New bank: every song needs a new prepare
            }
        }
    }
    // Samples for the open song; repeats while the user opens another song meanwhile
    for (;;) {
        Midi::Song scan;   // Copy of the events: open()/close() may run during the read
        uint32_t gen;
        {
            Lock l;
            if (err != Err::OK || !needPrepare()) {
                if (s_open && err == Err::OK && s_songGen == s_failGen) err = Err::SONG_TOO_BIG;
                if (s_open) {   // Re-apply the song state (programs via seek, mute, transpose)
                    s_synth.setTranspose(s_pendingTr);
                    applyMelodyGain();
                    applyMelodyMute();
                    s_player.seek(s_player.position(), s_synth);
                    if (s_previewPending && err == Err::OK) {
                        if (s_synth.previewNote(s_previewChannel, s_previewProgram, s_previewKey, 100)) {
                            s_previewActive = true;
                            s_previewFrames = Synth::RATE * 3 / 2;
                        } else {
                            ESP_LOGW(TAG, "instrument preview unavailable: channel %d program %d",
                                     s_previewChannel + 1, s_previewProgram + 1);
                            s_previewPending = false;
                        }
                    }
                    if (s_trReload && err == Err::OK) {
                        if (s_resumePlay && !s_previewActive) s_player.play(true);   // Continues where it stopped
                    }
                }
                s_previewPending = false;
                s_synth.clearPreviewPreset();
                s_trReload = s_resumePlay = false;
                s_fontErr = err;
                s_fontState = err == Err::OK ? FontState::READY : FontState::FAILED;
                break;
            }
            gen = s_songGen;
            scan.events = s_song.events;
            s_synth.setTranspose(s_pendingTr);   // The scan records the keys as they will play
        }
        s_loadPct = 0;
        const int64_t t0 = esp_log_timestamp();
        Sf::LoadErr le;
        {
            BoardSd::Guard g;
            le = s_synth.prepare(scan, &progressCb, nullptr);
        }
        s_synth.clearPreviewPreset();
        const Sf::PrepStats st = s_synth.stats();
        ESP_LOGI(TAG, "prepare (transpose %d): %s, %u bytes of budget %u, %u presets, %u zones (%u resampled, %u bytes saved), %u reads%s, %lld ms, PSRAM free %u",
                 s_synth.transpose(), Sf::loadErrName(le), (unsigned)st.bytes, (unsigned)st.budget, st.presets, st.regions,
                 st.resampled, (unsigned)st.resampledSaved, st.ranges,
                 st.thinned ? " (one zone per key)" : st.reduced ? " (one velocity layer)" : "", (long long)(esp_log_timestamp() - t0),
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
        Lock l;
        if (le == Sf::LoadErr::OK) {
            s_prepGen = gen;
        } else if (le == Sf::LoadErr::TOO_BIG && s_trReload && gen == s_songGen && s_pendingTr != s_prevTr) {
            // The new transpose does not fit: keep the previous one (prepared again) and tell the UI
            ESP_LOGW(TAG, "transpose %d does not fit, back to %d", s_pendingTr, s_prevTr);
            s_pendingTr = s_prevTr;
            s_trRejected = true;
            if (++s_songGen == 0) s_songGen = 1;
        } else if (le == Sf::LoadErr::TOO_BIG) {
            s_failGen = gen;
        } else {
            err = mapLoadErr(le);   // Memory or card trouble: stop here
        }
    }
    memoryMark("bank-task-end");
    vTaskDelete(nullptr);
}

// Caller holds s_mux. Starts the font task (state LOADING).
void startTask(bool openBank)
{
    s_taskOpenBank = openBank;
    if (s_open) {
        s_player.play(false);
        s_synth.allNotesOff();
    }
    s_loadPct = 0;
    s_fontState = FontState::LOADING;
    if (xTaskCreatePinnedToCore(fontTask, "sf2load", 6144, nullptr, 3, nullptr, 1) != pdPASS) {
        s_fontErr = Err::NO_MEMORY;
        s_fontState = FontState::FAILED;
        if (s_previewPending) {
            s_synth.clearPreviewPreset();
            s_previewPending = false;
            if (s_previewResume) s_player.play(true);
            s_previewResume = false;
        }
    }
}

bool synthBusy() { return s_fontState == FontState::LOADING; }

std::string currentRequest() { return s_open && !s_songSf2.empty() ? s_songSf2 : std::string(Config::get().karaFont); }

}  // namespace

namespace {

SemaphoreHandle_t libMux()
{
    static SemaphoreHandle_t m = xSemaphoreCreateMutex();
    return m;
}

void ensureFolders()
{
    BoardSd::ensureDir("/sdcard/otr");
    BoardSd::ensureDir(DIR_SONGS);
    BoardSd::ensureDir(DIR_SF2);
}

// Walks DIR_SONGS and every subfolder (explicit work list: no recursion, no depth limit; FAT has no
// links so it cannot loop). The SD guard is taken per folder so audio reads are not starved.
Err scanLib(KaraIndex::Library &lib)
{
    lib.clear();
    {
        BoardSd::Guard g;
        if (!BoardSd::mounted()) return Err::NO_SD;
        ensureFolders();
    }
    std::vector<std::string> todo{std::string()};
    bool rootOk = false;
    const int64_t t0 = esp_timer_get_time();
    while (!todo.empty() && !lib.full()) {
        const std::string rel = std::move(todo.back());
        todo.pop_back();
        const std::string abs = rel.empty() ? std::string(DIR_SONGS) : std::string(DIR_SONGS) + "/" + rel;
        BoardSd::Guard g;
        if (!BoardSd::mounted()) return Err::NO_SD;
        DIR *d = opendir(abs.c_str());
        if (!d) continue;
        if (rel.empty()) rootOk = true;
        while (dirent *e = readdir(d)) {
            if (e->d_name[0] == '.') continue;
            const std::string childRel = rel.empty() ? std::string(e->d_name) : rel + "/" + e->d_name;
            if (childRel.size() > KaraIndex::MAX_PATH) continue;
            const std::string childAbs = abs + "/" + e->d_name;
            bool isDir = e->d_type == DT_DIR;
            struct stat st;
            const bool song = !isDir && KaraIndex::songExt(e->d_name);
            if (e->d_type == DT_UNKNOWN || song) {
                if (stat(childAbs.c_str(), &st) != 0) continue;
                isDir = S_ISDIR(st.st_mode);
            }
            if (isDir) {
                todo.push_back(childRel);
                continue;
            }
            if (!song) continue;
            std::string title, artist;
            Midi::nameFromFile(e->d_name, title, artist);
            if (!lib.add(childRel.c_str(), title.c_str(), artist.c_str(), (uint32_t)st.st_size) && lib.full()) break;
        }
        closedir(d);
    }
    if (!rootOk) return Err::NO_SD;
    lib.sort();
    ESP_LOGI(TAG, "scan: %u songs in %u ms%s", (unsigned)lib.size(), (unsigned)((esp_timer_get_time() - t0) / 1000),
             lib.full() ? " (limit reached)" : "");
    return Err::OK;
}

// SD free space plus what the rotating log files occupy (they grow all the time and would make
// every index look stale). Rounded to whole clusters like FAT allocates them.
uint64_t freeWithoutLogs()
{
    BoardSd::Guard g;
    BoardSd::refreshUsage();
    uint64_t free = BoardSd::info().freeBytes;
    const uint32_t cl = BoardSd::clusterBytes();
    DIR *d = opendir(LOG_DIR);
    if (!d) return free;
    while (dirent *e = readdir(d)) {
        if (e->d_name[0] == '.') continue;
        const std::string p = std::string(LOG_DIR) + "/" + e->d_name;
        struct stat st;
        if (stat(p.c_str(), &st) != 0 || S_ISDIR(st.st_mode)) continue;
        uint64_t sz = (uint64_t)st.st_size;
        if (cl) sz = (sz + cl - 1) / cl * cl;
        free += sz;
    }
    closedir(d);
    return free;
}

uint32_t rootMtime()
{
    BoardSd::Guard g;
    struct stat st;
    return stat(DIR_SONGS, &st) == 0 ? (uint32_t)st.st_mtime : 0;
}

bool writeFile(const char *path, const std::vector<uint8_t> &data)
{
    BoardSd::Guard g;
    if (!BoardSd::mounted()) return false;
    FILE *f = fopen(path, "wb");
    if (!f) return false;
    const bool ok = fwrite(data.data(), 1, data.size(), f) == data.size();
    return fclose(f) == 0 && ok;
}

// Writes songs.idx. The file itself changes the free space, so it is written twice: the second
// write (same size, no new clusters) stores the free space measured after the first.
void writeIndex(const KaraIndex::Library &lib)
{
    KaraIndex::Header h;
    const time_t now = time(nullptr);
    h.scanTime = now > 1600000000 ? (uint32_t)now : 0;
    h.rootMtime = rootMtime();
    std::vector<uint8_t> buf;
    lib.encode(h, buf);
    if (!writeFile(IDX_FILE, buf)) {
        ESP_LOGW(TAG, "cannot write %s", IDX_FILE);
        return;
    }
    h.freeBytes = freeWithoutLogs();
    lib.encode(h, buf);
    writeFile(IDX_FILE, buf);
    BoardSd::refreshUsage();
}

bool readIndex(KaraIndex::Library &lib, KaraIndex::Header &h)
{
    std::vector<uint8_t, KaraIndex::PsAlloc<uint8_t>> data;
    {
        BoardSd::Guard g;
        if (!BoardSd::mounted()) return false;
        FILE *f = fopen(IDX_FILE, "rb");
        if (!f) return false;
        fseek(f, 0, SEEK_END);
        const long sz = ftell(f);
        fseek(f, 0, SEEK_SET);
        if (sz <= 0 || sz > 2 * 1024 * 1024) { fclose(f); return false; }
        data.resize((size_t)sz);
        const bool ok = fread(data.data(), 1, data.size(), f) == data.size();
        fclose(f);
        if (!ok) return false;
    }
    return lib.decode(data.data(), data.size(), h);
}

void publish(KaraIndex::Library &lib, Err e)
{
    xSemaphoreTake(libMux(), portMAX_DELAY);
    std::swap(s_lib, lib);
    s_libErr = e;
    s_libLoaded = e == Err::OK;
    if (++s_libGen == 0) s_libGen = 1;
    xSemaphoreGive(libMux());
}

}  // namespace

Err scan(std::vector<SongFile> &out)
{
    out.clear();
    KaraIndex::Library lib;
    const Err e = scanLib(lib);
    if (e != Err::OK) return e;
    out.reserve(lib.size());
    for (size_t i = 0; i < lib.size(); i++) out.push_back(songAt(lib, i));
    return Err::OK;
}

SongFile songAt(const KaraIndex::Library &lib, size_t i)
{
    SongFile f;
    f.path = std::string(DIR_SONGS) + "/" + lib.rel(i);
    f.title = lib.title(i);
    f.artist = lib.artist(i);
    f.size = lib.fileSize(i);
    f.dir = lib.dir(i);
    return f;
}

Err rescan()
{
    KaraIndex::Library lib;
    const Err e = scanLib(lib);
    if (e == Err::OK) writeIndex(lib);
    publish(lib, e);
    return e;
}

bool loadIndex()
{
    if (s_scanning) return true;   // The running scan publishes its result
    {
        BoardSd::Guard g;
        if (!BoardSd::mounted()) {
            KaraIndex::Library empty;
            publish(empty, Err::NO_SD);
            return true;   // Nothing to scan
        }
        ensureFolders();
    }
    KaraIndex::Library lib;
    KaraIndex::Header h;
    if (!readIndex(lib, h)) {
        ESP_LOGI(TAG, "songs.idx missing or invalid");
        return false;
    }
    const uint64_t free = freeWithoutLogs();
    const uint32_t mt = rootMtime();
    if (h.freeBytes != free || h.rootMtime != mt) {
        ESP_LOGI(TAG, "songs.idx stale (free %llu/%llu, mtime %u/%u)", (unsigned long long)h.freeBytes,
                 (unsigned long long)free, (unsigned)h.rootMtime, (unsigned)mt);
        return false;
    }
    ESP_LOGI(TAG, "songs.idx: %u songs", (unsigned)lib.size());
    publish(lib, Err::OK);
    return true;
}

Err load()
{
    if (loadIndex()) return s_libErr;
    return rescan();
}

bool libraryLoaded() { return s_libLoaded; }

void rescanAsync()
{
    if (s_scanning) return;
    s_scanning = true;
    if (xTaskCreatePinnedToCore([](void *) {
            rescan();
            s_scanning = false;
            vTaskDelete(nullptr);
        }, "karascan", 8192, nullptr, 2, nullptr, 0) != pdPASS)
        s_scanning = false;
}

bool scanning() { return s_scanning; }
Err libraryError() { return s_libErr; }
uint32_t libraryGen() { return s_libGen; }
void libraryLock() { xSemaphoreTake(libMux(), portMAX_DELAY); }
void libraryUnlock() { xSemaphoreGive(libMux()); }
const KaraIndex::Library &libraryRef() { return s_lib; }

Err openByPath(const char *path)
{
    if (!path) return Err::NO_FILE;
    std::string rel = path;
    const std::string root = std::string(DIR_SONGS) + "/";
    if (rel.compare(0, root.size(), root) == 0) rel.erase(0, root.size());
    while (!rel.empty() && rel[0] == '/') rel.erase(0, 1);
    if (!KaraIndex::safeRel(rel.c_str()) || !KaraIndex::songExt(rel.c_str())) return Err::NO_FILE;
    if (synthBusy()) return Err::BUSY;
    SongFile sf;
    bool found = false;
    withLibrary([&](const KaraIndex::Library &lib, uint32_t) {
        const int i = lib.find(rel.c_str());
        if (i >= 0) sf = songAt(lib, (size_t)i), found = true;
    });
    if (!found) {   // Not indexed yet (copied after the last scan): build the entry from the name
        sf.path = root + rel;
        const size_t slash = rel.find_last_of('/');
        sf.dir = slash == std::string::npos ? std::string() : rel.substr(0, slash);
        Midi::nameFromFile(slash == std::string::npos ? rel.c_str() : rel.c_str() + slash + 1, sf.title, sf.artist);
        sf.size = 0;
    }
    const Err e = open(sf);
    if (e != Err::OK) return e;
    s_cur = sf;
    s_autoPlay = true;
    ensureFont(false);
    if (++s_extGen == 0) s_extGen = 1;
    ESP_LOGI(TAG, "web: open %s", rel.c_str());
    return Err::OK;
}

uint32_t externalOpenGen() { return s_extGen; }
SongFile current() { return s_open ? s_cur : SongFile{}; }

void ensureFont(bool retryFailed)
{
    if (!s_mux) s_mux = xSemaphoreCreateMutex();
    const FontState st = s_fontState;
    if (st == FontState::LOADING) return;
    const std::string req = currentRequest();
    if (req == s_fontReq && (st == FontState::READY || (st == FontState::FAILED && !retryFailed))) return;
    Lock l;
    if (s_fontState == FontState::LOADING) return;   // v0.13.7: the web may call this too
    s_fontReq = req;
    snprintf(s_reqName, sizeof(s_reqName), "%.63s", req.c_str());
    if (retryFailed) s_failGen = 0;
    startTask(true);
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

void setVoiceLimit(uint8_t voices)
{
    if (!Config::validKaraVoices(voices) || voices > HwProfile::active().maxKaraokeVoices) return;
    Config::get().karaVoices = voices;
    Config::save();
    Lock l;
    s_synth.setMaxVoices(voices);
}

const char *songFont() { return s_songSf2.c_str(); }

void setSongFont(const char *name)
{
    Lock l;
    if (!s_open) return;
    s_songSf2 = name ? name : "";
    if (s_songSf2.size() > 47) s_songSf2.resize(47);
}

FontState fontState() { return s_fontState; }
Err fontError() { return s_fontErr; }
const char *fontName() { return s_fontName; }
bool fontFallback()
{
    const std::string requested = currentRequest();
    return s_fontState == FontState::READY && !requested.empty() && requested != s_fontName;
}

void unloadFont()
{
    if (s_fontState == FontState::LOADING) return;
    close();
    Lock l;
    s_synth.unload();
    s_fontName[0] = 0;
    s_fontReq.clear();
    s_prepGen = s_failGen = 0;
    s_fontState = FontState::NONE;
    memoryMark("bank-unloaded");
}

int loadPct() { return s_loadPct; }

FontInfo fontInfo()
{
    FontInfo fi{};
    Lock l;
    if (synthBusy() || !s_synth.bankOpen()) return fi;
    const Sf::PrepStats &st = s_synth.stats();
    fi.fileSize = (uint32_t)s_synth.fileSize();
    fi.bankPresets = (uint16_t)s_synth.presetCount();
    fi.prepared = s_synth.loaded();
    if (fi.prepared) fi.bytes = st.bytes, fi.presets = st.presets, fi.regions = st.regions, fi.reduced = st.reduced;
    return fi;
}

Effects effects()
{
    const Sf::Effects e = cfgEffects();
    return Effects{e.reverb, e.chorus, e.reverbLevel, e.chorusLevel};
}

void setEffects(const Effects &e)
{
    AppConfig &c = Config::get();
    c.karaRev = e.reverb ? 1 : 0;
    c.karaCho = e.chorus ? 1 : 0;
    c.karaRevLvl = e.reverbLevel > 100 ? 100 : e.reverbLevel;
    c.karaChoLvl = e.chorusLevel > 100 ? 100 : e.chorusLevel;
    {
        Lock l;
        if (!synthBusy()) s_synth.setEffects(cfgEffects());
    }
    Audio::takeExternalStats();   // Start a clean load window so the next diag line shows only the new setting
    ESP_LOGI(TAG, "effects: reverb %s/%u chorus %s/%u", c.karaRev ? "on" : "off", (unsigned)c.karaRevLvl,
             c.karaCho ? "on" : "off", (unsigned)c.karaChoLvl);
    Config::save();
}

Err open(const SongFile &sf)
{
    memoryMark("song-open-start");
    if (songSettingsDirty()) return Err::UNSAVED;
    RegistryLock registryLock;
    const Err registryErr = loadRegistry();
    if (registryErr != Err::OK) return registryErr;
    close();
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
    KaraSettings::Song settings;
    const Err settingsErr = readSongSettings(sf.path, settings);
    if (settingsErr != Err::OK) return settingsErr;
    Lock l;
    Midi::Err e = Midi::parse(data.data(), data.size(), s_song);
    if (e == Midi::Err::TOO_BIG) return Err::FILE_TOO_BIG;
    if (e != Midi::Err::OK) {
        ESP_LOGW(TAG, "%s: %s", sf.path.c_str(), Midi::errName(e));
        s_song.clear();
        return Err::FILE_BAD;
    }
    s_plainLyrics.clear();
    if (s_song.lines.empty()) {
        const auto lyricFailure = [](Err err) {
            s_song.clear();
            return err;
        };
        std::string sidecarPath = sf.path;
        const size_t dot = sidecarPath.find_last_of('.');
        const size_t slash = sidecarPath.find_last_of('/');
        if (dot != std::string::npos && (slash == std::string::npos || dot > slash))
            sidecarPath.replace(dot, std::string::npos, ".lyr");
        else
            sidecarPath.clear();
        if (!sidecarPath.empty()) {
            std::vector<uint8_t> sidecarData;
            bool found = false;
            {
                BoardSd::Guard g;
                if (!BoardSd::mounted()) return lyricFailure(Err::NO_SD);
                FILE *f = fopen(sidecarPath.c_str(), "rb");
                if (f) {
                    found = true;
                    if (fseek(f, 0, SEEK_END) != 0) {
                        fclose(f);
                        return lyricFailure(Err::LYRICS_BAD);
                    }
                    const long sidecarSize = ftell(f);
                    if (sidecarSize <= 0) {
                        fclose(f);
                        return lyricFailure(Err::LYRICS_BAD);
                    }
                    if ((size_t)sidecarSize > KaraLyrics::MAX_FILE) {
                        fclose(f);
                        return lyricFailure(Err::LYRICS_TOO_BIG);
                    }
                    if (fseek(f, 0, SEEK_SET) != 0) {
                        fclose(f);
                        return lyricFailure(Err::LYRICS_BAD);
                    }
                    sidecarData.resize((size_t)sidecarSize);
                    const bool ok = fread(sidecarData.data(), 1, sidecarData.size(), f) == sidecarData.size();
                    fclose(f);
                    if (!ok) return lyricFailure(Err::LYRICS_BAD);
                } else if (errno != ENOENT) {
                    return lyricFailure(Err::LYRICS_BAD);
                }
            }
            if (found) {
                KaraLyrics::Document lyrics;
                const KaraLyrics::Err lyricErr =
                    KaraLyrics::parse((const char *)sidecarData.data(), sidecarData.size(), lyrics);
                if (lyricErr == KaraLyrics::Err::TOO_BIG || lyricErr == KaraLyrics::Err::TOO_MANY_LINES ||
                    lyricErr == KaraLyrics::Err::LINE_TOO_LONG)
                    return lyricFailure(Err::LYRICS_TOO_BIG);
                if (lyricErr != KaraLyrics::Err::OK) return lyricFailure(Err::LYRICS_BAD);
                if (lyrics.timed) {
                    s_song.text.clear();
                    s_song.syls.clear();
                    s_song.lines.clear();
                    for (size_t i = 0; i < lyrics.lines.size(); i++) {
                        const KaraLyrics::Line &line = lyrics.lines[i];
                        const uint32_t first = (uint32_t)s_song.syls.size();
                        const uint32_t textOffset = (uint32_t)s_song.text.size();
                        s_song.text += line.text;
                        s_song.syls.push_back(Midi::Syllable{line.us, textOffset, (uint16_t)line.text.size(),
                                                              Midi::SYL_LINE});
                        const uint32_t endUs = i + 1 < lyrics.lines.size() ? lyrics.lines[i + 1].us : s_song.lengthUs;
                        s_song.lines.push_back(Midi::Line{line.us, endUs, first, 1, false});
                    }
                } else {
                    for (size_t i = 0; i < lyrics.lines.size(); i++) {
                        if (i) s_plainLyrics.push_back('\n');
                        s_plainLyrics += lyrics.lines[i].text;
                    }
                }
            }
        }
    }
    s_crc = Midi::crc32(data.data(), data.size());
    s_size = (uint32_t)data.size();
    std::vector<uint8_t>().swap(data);   // Parsing/CRC finished; do not overlap this copy with synth preparation
    const size_t slash = sf.path.find_last_of('/');
    s_fileName = slash == std::string::npos ? sf.path : sf.path.substr(slash + 1);
    s_melody = s_song.melody;
    s_player.load(&s_song);
    applySongSettings(settings, false);
    s_trReload = s_resumePlay = false;
    s_songGen++;
    if (s_songGen == 0) s_songGen = 1;   // 0 = "none"
    if (!synthBusy()) {
        s_synth.setTranspose(s_pendingTr);
        s_player.seek(0, s_synth);
    }
    s_open = true;
    s_savedSettings = currentSettings();
    const std::string rel = sf.path.substr(strlen(DIR_SONGS) + 1);
    s_reg.rememberSongPath(s_crc, s_size, rel.c_str());
    s_cur = sf;
    s_autoPlay = false;
    // Same bank as loaded: load this song's samples now. A different bank is started by the UI
    // (ensureFont), which prepares the song after opening the bank. A running task picks it up.
    if (!synthBusy() && s_synth.bankOpen() && currentRequest() == s_fontReq) startTask(false);
    ESP_LOGI(TAG, "%s: %u events, %u lines, melody ch %d%s, crc %08x", s_fileName.c_str(),
             (unsigned)s_song.events.size(), (unsigned)s_song.lines.size(), s_melody + 1,
             s_melodyReg ? " (register)" : "", (unsigned)s_crc);
    Audio::setExternal(&renderCb, nullptr);
    KaraWeb::setSong(true, s_fileName.c_str(), s_plainLyrics.empty() ? nullptr : s_plainLyrics.c_str());
    startDiag();
    memoryMark("song-open-end");
    return Err::OK;
}

void close()
{
    stopDiag();
    KaraWeb::setSong(false, nullptr);
    Audio::setExternal(nullptr, nullptr);
    Lock l;
    if (!s_open) return;
    s_open = false;
    s_autoPlay = false;
    s_previewActive = s_previewResume = false;
    s_previewPending = false;
    s_previewChannel = s_previewProgram = -1;
    s_previewFrames = 0;
    s_player.play(false);
    if (!synthBusy()) s_synth.reset();
    s_player.load(nullptr);
    s_song.clear();   // Frees the event/lyric buffers (PSRAM)
    s_plainLyrics.clear();
    memoryMark("song-closed");
}

bool isOpen() { return s_open; }
const Midi::Song *song() { return s_open ? &s_song : nullptr; }
std::string plainLyrics()
{
    Lock l;
    return s_open ? s_plainLyrics : std::string();
}

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
    Lock l;
    if (s_open) s_player.setTempo(pct);
}

int tempo() { return s_player.tempo(); }

void setTranspose(int semis)
{
    {
        Lock l;
        if (!s_open || synthBusy()) return;
        semis = semis < -12 ? -12 : semis > 12 ? 12 : semis;
        if (semis == s_synth.transpose()) return;
        if (s_synth.selective() && s_synth.loaded()) {
            // Only the keys of the current transpose are loaded: prepare again (short silence, the
            // song continues from the same position once the samples fit)
            s_prevTr = s_synth.transpose();
            s_pendingTr = semis;
            s_trReload = true;
            s_resumePlay = s_player.playing();
            if (++s_songGen == 0) s_songGen = 1;
            startTask(false);
            return;
        }
        s_synth.setTranspose(semis);   // Whole bank in memory (or silent mode): just shift
        s_pendingTr = s_synth.transpose();
    }
}

int transpose() { return s_open ? s_pendingTr : s_synth.transpose(); }

bool takeTransposeRejected()
{
    const bool r = s_trRejected;
    s_trRejected = false;
    return r;
}

bool takeOverloadStopped()
{
    const bool r = s_overloadStop;
    s_overloadStop = false;
    return r;
}

void setMelody(int ch)
{
    Lock l;
    if (!s_open || synthBusy() || ch < -1 || ch > 15) return;
    s_melody = ch;
    s_melodyReg = ch != s_song.melody;
    applyMelodyGain();
    applyMelodyMute();
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

uint8_t songVolume() { return s_songVolume; }

void setSongVolume(uint8_t v)
{
    Lock l;
    if (s_open) s_songVolume = v > 200 ? 200 : v;
}

uint8_t melodyGain() { return s_melodyGain; }

void setMelodyGain(uint8_t v)
{
    Lock l;
    if (!s_open) return;
    s_melodyGain = v > 200 ? 200 : v;
    if (!synthBusy()) applyMelodyGain();
}

std::vector<InstrumentMapping> instrumentMappings()
{
    Lock l;
    std::vector<InstrumentMapping> out;
    out.reserve(s_instrumentMaps.size());
    for (const auto &m : s_instrumentMaps) out.push_back({m.channel, m.sourceProgram, m.replacementProgram});
    return out;
}

Err setInstrumentMapping(uint8_t channel, uint8_t sourceProgram, uint8_t replacementProgram)
{
    if (channel > 15 || sourceProgram > 127 || replacementProgram > 127) return Err::BAD_INPUT;
    Lock l;
    if (!s_open) return Err::NO_FILE;
    if (synthBusy()) return Err::BUSY;
    auto it = std::find_if(s_instrumentMaps.begin(), s_instrumentMaps.end(), [&](const auto &m) {
        return m.channel == channel && m.sourceProgram == sourceProgram;
    });
    if (it == s_instrumentMaps.end()) {
        if (s_instrumentMaps.size() >= KaraSettings::MAX_INSTRUMENT_MAPS) return Err::BAD_INPUT;
        s_instrumentMaps.push_back({channel, sourceProgram, replacementProgram});
    } else {
        it->replacementProgram = replacementProgram;
    }
    applyInstrumentMaps();
    const bool resume = s_player.playing();
    s_trReload = true;
    s_resumePlay = resume;
    s_prevTr = s_pendingTr;
    if (++s_songGen == 0) s_songGen = 1;
    startTask(false);
    return Err::OK;
}

Err removeInstrumentMapping(uint8_t channel, uint8_t sourceProgram)
{
    if (channel > 15 || sourceProgram > 127) return Err::BAD_INPUT;
    Lock l;
    if (!s_open) return Err::NO_FILE;
    if (synthBusy()) return Err::BUSY;
    const auto it = std::find_if(s_instrumentMaps.begin(), s_instrumentMaps.end(), [&](const auto &m) {
        return m.channel == channel && m.sourceProgram == sourceProgram;
    });
    if (it == s_instrumentMaps.end()) return Err::OK;
    s_instrumentMaps.erase(it);
    applyInstrumentMaps();
    const bool resume = s_player.playing();
    s_trReload = true;
    s_resumePlay = resume;
    s_prevTr = s_pendingTr;
    if (++s_songGen == 0) s_songGen = 1;
    startTask(false);
    return Err::OK;
}

Err previewInstrument(uint8_t channel, uint8_t program)
{
    if (channel > 15 || program > 127) return Err::BAD_INPUT;
    Lock l;
    if (!s_open) return Err::NO_FILE;
    if (synthBusy() || !s_synth.bankOpen() || s_previewActive || s_previewPending) return Err::BUSY;
    s_previewChannel = channel;
    s_previewProgram = program;
    s_previewKey = 60;
    s_previewPosition = s_player.position();
    s_previewResume = s_player.playing();
    s_previewPending = true;
    s_synth.setPreviewPreset(channel, program);
    s_trReload = true;
    s_resumePlay = s_previewResume;
    s_prevTr = s_pendingTr;
    if (++s_songGen == 0) s_songGen = 1;
    startTask(false);
    return Err::OK;
}

bool songSettingsDirty()
{
    Lock l;
    if (!s_open) return false;
    const KaraSettings::Song e = currentSettings();
    if (e.soundfont != s_savedSettings.soundfont || e.midiVolume != s_savedSettings.midiVolume ||
        e.melodyChannel != s_savedSettings.melodyChannel || e.melodyMuted != s_savedSettings.melodyMuted ||
        e.melodyGainPct != s_savedSettings.melodyGainPct || e.tempo != s_savedSettings.tempo ||
        e.transpose != s_savedSettings.transpose || e.instrumentMaps.size() != s_savedSettings.instrumentMaps.size())
        return true;
    for (size_t i = 0; i < e.instrumentMaps.size(); i++) {
        const auto &a = e.instrumentMaps[i];
        const auto &b = s_savedSettings.instrumentMaps[i];
        if (a.channel != b.channel || a.sourceProgram != b.sourceProgram ||
            a.replacementProgram != b.replacementProgram)
            return true;
    }
    return false;
}

Err saveSongSettings()
{
    RegistryLock registryLock;
    KaraSettings::Song entry;
    std::string path, text;
    uint32_t generation;
    {
        Lock l;
        if (!s_open) return Err::NO_FILE;
        if (synthBusy()) return Err::BUSY;
        entry = currentSettings();
        path = s_cur.path;
        generation = s_songGen;
    }
    if (KaraSettings::serialize(entry, text) != KaraSettings::Err::OK ||
        !writeSongSettings(path, text))
        return Err::SAVE_FAILED;
    {
        Lock l;
        if (!s_open || generation != s_songGen || path != s_cur.path) return Err::BUSY;
        s_savedSettings = entry;
    }
    return Err::OK;
}

Err discardSongSettings()
{
    KaraSettings::Song saved;
    {
        Lock l;
        if (!s_open) return Err::NO_FILE;
        if (synthBusy()) return Err::BUSY;
        saved = s_savedSettings;
        applySongSettings(saved, false);
        s_trReload = false;
        s_resumePlay = false;
        s_trRejected = false;
        if (!synthBusy()) {
            s_synth.setTranspose(saved.transpose);
            s_player.seek(s_player.position(), s_synth);
        }
    }
    ensureFont(true);
    return Err::OK;
}

namespace {
Err songKeyByPath(const char *path, Midi::SongKey &key)
{
    if (!path || !KaraIndex::safeRel(path) || !KaraIndex::songExt(path)) return Err::BAD_INPUT;
    BoardSd::Guard guard;
    if (!BoardSd::mounted()) return Err::NO_SD;
    const std::string full = std::string(DIR_SONGS) + "/" + path;
    FILE *f = fopen(full.c_str(), "rb");
    if (!f) return Err::NO_FILE;
    key = Midi::SongKey{};
    key.path = path;
    uint8_t buf[1024];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) != 0) {
        key.size += (uint32_t)n;
        if (key.size > Midi::MAX_FILE) { fclose(f); return Err::FILE_TOO_BIG; }
        key.crc = Midi::crc32(buf, n, key.crc);
    }
    const bool ok = !ferror(f) && key.size > 0;
    fclose(f);
    return ok ? Err::OK : Err::FILE_BAD;
}

Err validateSongFont(const char *font)
{
    if (!font || !KaraSettings::validSoundfont(font)) return Err::BAD_INPUT;
    if (!*font) return Err::OK;
    std::vector<FontFile> fonts;
    const Err err = scanFonts(fonts);
    if (err != Err::OK) return err;
    return std::any_of(fonts.begin(), fonts.end(), [font](const FontFile &f) {
        return f.name == font && f.err == Err::OK;
    }) ? Err::OK : Err::BAD_INPUT;
}
}

Err playlists(std::vector<PlaylistView> &out, uint16_t detailId, size_t offset)
{
    RegistryLock registryLock;
    const Err err = loadRegistry();
    if (err == Err::OK) {
        out.clear();
        for (const auto &list : s_reg.playlists()) {
            if (detailId && detailId != list.id) continue;
            out.push_back(PlaylistView{list.id, list.name, list.songs.size(), {}});
            if (detailId) {
                const size_t from = std::min(offset, list.songs.size());
                const size_t to = std::min(from + 100, list.songs.size());
                out.back().songs.assign(list.songs.begin() + from, list.songs.begin() + to);
            }
        }
    }
    return err;
}

Err editPlaylist(const char *action, uint16_t &id, const char *name,
                 const char *path, uint32_t crc, uint32_t size)
{
    RegistryLock registryLock;
    Err err = loadRegistry();
    if (err != Err::OK) return err;
    Midi::Registry next = s_reg;
    bool ok = false;
    if (!strcmp(action, "create")) { id = next.createPlaylist(name); ok = id != 0; }
    else if (!strcmp(action, "rename")) ok = next.renamePlaylist(id, name);
    else if (!strcmp(action, "delete")) ok = next.deletePlaylist(id);
    else if (!strcmp(action, "add")) {
        Midi::SongKey key;
        err = songKeyByPath(path, key);
        if (err != Err::OK) return err;
        ok = next.setPlaylistSong(id, key.crc, key.size, true, key.path.c_str());
    } else if (!strcmp(action, "remove")) {
        ok = next.setPlaylistSong(id, crc, size, false);
    } else if (!strcmp(action, "up") || !strcmp(action, "down")) {
        ok = next.movePlaylistSong(id, crc, size, !strcmp(action, "up") ? -1 : 1);
    }
    if (!ok) return Err::BAD_INPUT;
    if (!writeRegistry(next.save())) return Err::SAVE_FAILED;
    s_reg = std::move(next);
    return Err::OK;
}

Err songFontByPath(const char *path, std::string &font)
{
    Midi::SongKey key;
    Err err = songKeyByPath(path, key);
    if (err != Err::OK) return err;
    const std::string full = std::string(DIR_SONGS) + "/" + key.path;
    KaraSettings::Song settings;
    err = readSongSettings(full, settings);
    if (err == Err::OK) font = settings.soundfont;
    return err;
}

Err saveSongFontByPath(const char *path, const char *font)
{
    const Err fontErr = validateSongFont(font);
    if (fontErr != Err::OK) return fontErr;
    RegistryLock registryLock;
    Midi::SongKey key;
    Err err = songKeyByPath(path, key);
    if (err != Err::OK) return err;
    bool active;
    KaraSettings::Song settings;
    {
        Lock l;
        active = s_open && s_crc == key.crc && s_size == key.size;
        if (active && synthBusy()) return Err::BUSY;
        if (active) settings = currentSettings();
    }
    if (active && songSettingsDirty()) return Err::UNSAVED;
    if (!active) {
        const std::string full = std::string(DIR_SONGS) + "/" + key.path;
        err = readSongSettings(full, settings);
        if (err != Err::OK) return err;
    }
    settings.soundfont = font;
    std::string text;
    if (KaraSettings::serialize(settings, text) != KaraSettings::Err::OK ||
        !writeSongSettings(std::string(DIR_SONGS) + "/" + key.path, text))
        return Err::SAVE_FAILED;
    if (active) {
        {
            Lock l;
            s_songSf2 = font;
            s_savedSettings = settings;
        }
        ensureFont();
    }
    return Err::OK;
}

View view()
{
    View v{};
    Lock l;
    if (!s_open) return v;
    v.posUs = s_player.position();
    v.lengthUs = s_song.lengthUs;
    v.line = s_song.lineAt(v.posUs);
    v.syl = s_song.sylAt(v.posUs);
    const KaraStream::Countdown cd = KaraStream::countdownAt(s_song, v.posUs);
    v.cdDots = cd.dots;
    v.cdLit = cd.elapsed;
    v.playing = s_player.playing();
    v.finished = s_player.finished();
    v.voices = synthBusy() ? 0 : s_synth.activeVoices();
    v.maxVoices = s_synth.maxVoices();
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
    case Err::SONG_TOO_BIG: return tr(Str::KARA_ERR_SONG_BIG);
    case Err::BUSY: return tr(Str::KARA_WAIT_FONT);
    case Err::LYRICS_BAD: return tr(Str::KARA_ERR_LYRICS_BAD);
    case Err::LYRICS_TOO_BIG: return tr(Str::KARA_ERR_LYRICS_BIG);
    case Err::SAVE_FAILED: return tr(Str::SET_SAVE_FAILED);
    case Err::UNSAVED: return tr(Str::KARA_UNSAVED);
    case Err::BAD_INPUT: return tr(Str::KARA_ERR_SETTINGS);
    case Err::SETTINGS_BAD: return tr(Str::KARA_ERR_SONG_SETTINGS);
    }
    return "";
}

}  // namespace Karaoke
