// karaoke.cpp - see karaoke.h.
#include "karaoke.h"

#include <dirent.h>
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
uint32_t s_lastUnderruns = 0;

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
        if (s_autoPlay) {   // v0.13.7: song opened from the web picker
            s_autoPlay = false;
            if (s_player.finished()) s_player.seek(0, s_synth);
            s_player.play(true);
        }
        s_player.advance((uint32_t)((uint64_t)n * 1000000u / Synth::RATE), s_synth);
        s_synth.render(acc, n, gainQ8);
        KaraWeb::onRender(s_song, s_player.position());   // v0.13.5: lyric events for the web stream
        const int v = s_synth.activeVoices();
        if (v > s_voicesPeak) s_voicesPeak = v;
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

// Every 5 s while a song is open: render load (average/peak in % of a mixer block), late blocks and
// voices, to find the cause of dropouts on the device
void diagCb(void *)
{
    const Audio::ExternalStats st = Audio::takeExternalStats();
    const int peak = s_voicesPeak;
    s_voicesPeak = 0;
    const uint32_t newUnder = st.underruns - s_lastUnderruns;
    s_lastUnderruns = st.underruns;
    if (!s_open || s_fontState == FontState::LOADING || !st.blocks) return;
    ESP_LOGI(TAG, "diag: load avg %u.%02u%% max %u.%02u%%, underruns %u (+%u), voices peak %d/%d, %s",
             (unsigned)(st.avgLoad100 / 100), (unsigned)(st.avgLoad100 % 100), (unsigned)(st.maxLoad100 / 100),
             (unsigned)(st.maxLoad100 % 100), (unsigned)st.underruns, (unsigned)newUnder, peak, s_synth.maxVoices(),
             s_player.playing() ? "playing" : "paused");
}

void startDiag()
{
    if (!s_diagTimer) {
        const esp_timer_create_args_t a = {.callback = &diagCb, .arg = nullptr, .dispatch_method = ESP_TIMER_TASK,
                                           .name = "karadiag", .skip_unhandled_events = true};
        if (esp_timer_create(&a, &s_diagTimer) != ESP_OK) return;
    }
    Audio::takeExternalStats();   // Start a fresh period
    s_lastUnderruns = Audio::status().underruns;
    s_voicesPeak = 0;
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
        s_synth.setMaxVoices(Config::get().karaVoices);
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
    std::string regText;   // Register update after an accepted transpose
    for (;;) {
        Midi::Song scan;   // Copy of the events: open()/close() may run during the read
        uint32_t gen;
        {
            Lock l;
            if (err != Err::OK || !needPrepare()) {
                if (s_open && err == Err::OK && s_songGen == s_failGen) err = Err::SONG_TOO_BIG;
                if (s_open) {   // Re-apply the song state (programs via seek, mute, transpose)
                    s_synth.setTranspose(s_pendingTr);
                    applyMelodyMute();
                    s_player.seek(s_player.position(), s_synth);
                    if (s_trReload && err == Err::OK) {
                        regText = updateRegistry();
                        if (s_resumePlay) s_player.play(true);   // Continues where it stopped
                    }
                }
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
    writeRegistry(regText);
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
    if (!Config::validKaraVoices(voices)) return;
    Config::get().karaVoices = voices;
    Config::save();
    Lock l;
    s_synth.setMaxVoices(voices);
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
    Config::save();
}

Err open(const SongFile &sf)
{
    memoryMark("song-open-start");
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
    std::vector<uint8_t>().swap(data);   // Parsing/CRC finished; do not overlap this copy with synth preparation
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
    s_trReload = s_resumePlay = false;
    s_songGen++;
    if (s_songGen == 0) s_songGen = 1;   // 0 = "none"
    if (!synthBusy()) {
        s_synth.setTranspose(tr);
        s_player.seek(0, s_synth);
        applyMelodyMute();
    }
    s_open = true;
    s_cur = sf;
    s_autoPlay = false;
    // Same bank as loaded: load this song's samples now. A different bank is started by the UI
    // (ensureFont), which prepares the song after opening the bank. A running task picks it up.
    if (!synthBusy() && s_synth.bankOpen() && currentRequest() == s_fontReq) startTask(false);
    ESP_LOGI(TAG, "%s: %u events, %u lines, melody ch %d%s, crc %08x", s_fileName.c_str(),
             (unsigned)s_song.events.size(), (unsigned)s_song.lines.size(), s_melody + 1,
             s_melodyReg ? " (register)" : "", (unsigned)s_crc);
    Audio::setExternalVolume(s_volume);
    Audio::setExternal(&renderCb, nullptr);
    KaraWeb::setSong(true, s_fileName.c_str());
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
    s_player.play(false);
    if (!synthBusy()) s_synth.reset();
    s_player.load(nullptr);
    s_song.clear();   // Frees the event/lyric buffers (PSRAM)
    memoryMark("song-closed");
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
        semis = semis < -12 ? -12 : semis > 12 ? 12 : semis;
        if (semis == s_synth.transpose()) return;
        if (s_synth.selective() && s_synth.loaded()) {
            // Only the keys of the current transpose are loaded: prepare again (short silence, the
            // song continues from the same position; the register is written once it fits)
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
        text = updateRegistry();
    }
    writeRegistry(text);
}

int transpose() { return s_open ? s_pendingTr : s_synth.transpose(); }

bool takeTransposeRejected()
{
    const bool r = s_trRejected;
    s_trRejected = false;
    return r;
}

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
    Config::get().karaVolume = s_volume;
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
    }
    return "";
}

}  // namespace Karaoke
