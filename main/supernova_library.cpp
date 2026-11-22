// supernova_library.cpp - see supernova_library.h
#include "supernova_library.h"

#include <dirent.h>
#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#include <algorithm>
#include <atomic>
#include <vector>

#include "board_sd.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

namespace SupernovaLibrary {
namespace {

constexpr const char *TAG = "supernova_lib";
constexpr const char *ROOT = "/sdcard/otr/supernova";
constexpr const char *IDX = "/sdcard/otr/supernova/supernova.idx";
constexpr const char *IDX_TMP = "/sdcard/otr/supernova/supernova.tmp";
constexpr size_t MAX_IDX_BYTES = 2 * 1024 * 1024;

SemaphoreHandle_t s_mutex = nullptr;
SupernovaIndex::Library s_lib;
std::atomic<uint32_t> s_gen{0};
std::atomic<bool> s_scanning{false};
std::atomic<bool> s_catalogError{false};
std::atomic<uint32_t> s_done{0}, s_total{0};
bool s_loaded = false;
bool s_force = false;

bool endsWithExt(const std::string &name, const char *ext)
{
    const size_t extLen = strlen(ext);
    if (name.size() <= extLen) return false;
    const size_t start = name.size() - extLen;
    for (size_t i = 0; i < extLen; i++) {
        char a = name[start + i], b = ext[i];
        if (a >= 'A' && a <= 'Z') a += 'a' - 'A';
        if (b >= 'A' && b <= 'Z') b += 'a' - 'A';
        if (a != b) return false;
    }
    return true;
}

bool loadCache(SupernovaIndex::Library &out)
{
    BoardSd::Guard guard;
    if (!BoardSd::mounted()) return false;
    FILE *f = fopen(IDX, "rb");
    if (!f) return false;
    std::vector<uint8_t> data;
    bool ok = false;
    if (fseek(f, 0, SEEK_END) == 0) {
        const long size = ftell(f);
        if (size > 0 && (size_t)size <= MAX_IDX_BYTES && fseek(f, 0, SEEK_SET) == 0) {
            data.resize((size_t)size);
            ok = fread(data.data(), 1, data.size(), f) == data.size();
        }
    }
    fclose(f);
    return ok && out.decode(data.data(), data.size());
}

void saveCache(const std::vector<uint8_t> &data)
{
    BoardSd::Guard guard;
    if (!BoardSd::mounted()) return;
    FILE *f = fopen(IDX_TMP, "wb");
    if (!f) return;
    const bool ok = fwrite(data.data(), 1, data.size(), f) == data.size();
    fclose(f);
    if (!ok) {
        remove(IDX_TMP);
        return;
    }
    remove(IDX);   // FATFS does not replace an existing destination
    if (rename(IDX_TMP, IDX) != 0) ESP_LOGW(TAG, "could not store %s", IDX);
}

// Reads the start of the lyrics file for the tags
void readTags(const std::string &path, SupernovaIndex::Tags &tags)
{
    std::vector<char> buf(SupernovaIndex::TAG_SCAN_BYTES);
    size_t n = 0;
    {
        BoardSd::Guard guard;
        if (!BoardSd::mounted()) return;
        FILE *f = fopen(path.c_str(), "rb");
        if (!f) return;
        n = fread(buf.data(), 1, buf.size(), f);
        fclose(f);
    }
    SupernovaIndex::parseTags(buf.data(), n, tags);
}

constexpr const char *SYNC = "/sdcard/otr/supernova/supernova.sync";
constexpr const char *SYNC_TMP = "/sdcard/otr/supernova/supernova.sntmp";
constexpr size_t MAX_SYNC_BYTES = 512 * 1024;
SupernovaSync::Store s_sync;   // Only touched from the LVGL task
bool s_syncLoaded = false;

struct Dir {
    std::string name;
    uint32_t mtime;
};

void scanTask(void *)
{
    bool force;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    force = s_force;
    xSemaphoreGive(s_mutex);

    std::vector<Dir> dirs;
    bool ok = false;
    {
        BoardSd::Guard guard;
        if (BoardSd::mounted() && BoardSd::ensureDir(ROOT)) {
            if (DIR *d = opendir(ROOT)) {
                ok = true;
                while (dirent *e = readdir(d)) {
                    if (e->d_name[0] == '.') continue;
                    const std::string path = std::string(ROOT) + "/" + e->d_name;
                    struct stat st {};
                    if (stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) dirs.push_back({e->d_name, (uint32_t)st.st_mtime});
                }
                closedir(d);
            }
        }
    }
    s_catalogError = !ok;
    std::sort(dirs.begin(), dirs.end(), [](const Dir &a, const Dir &b) { return a.name < b.name; });
    s_done = 0;
    s_total = (uint32_t)dirs.size();

    if (ok) {
        // Reuse entries whose folder time is unchanged (the live library stays readable meanwhile)
        SupernovaIndex::Library fresh;
        bool changed = false;
        size_t reused = 0;
        for (const Dir &d : dirs) {
            if (fresh.full()) break;
            bool done = false;
            if (!force) {
                xSemaphoreTake(s_mutex, portMAX_DELAY);
                const int i = s_lib.find(d.name.c_str());
                if (i >= 0 && s_lib.mtime((size_t)i) == d.mtime) {
                    fresh.add(s_lib.folder((size_t)i), s_lib.artist((size_t)i), s_lib.title((size_t)i),
                              s_lib.genre((size_t)i), s_lib.language((size_t)i), s_lib.year((size_t)i), d.mtime);
                    done = true;
                    reused++;
                }
                xSemaphoreGive(s_mutex);
            }
            if (!done) {
                changed = true;
                Media media;
                bool complete;
                {
                    BoardSd::Guard guard;
                    complete = findMedia(folderPath(d.name.c_str()), media);
                }
                if (complete) {
                    SupernovaIndex::Tags tags;
                    readTags(media.lyrics, tags);
                    std::string fa, ft;
                    SupernovaIndex::namesFromFolder(d.name, fa, ft);
                    fresh.add(d.name.c_str(), tags.artist.empty() ? fa.c_str() : tags.artist.c_str(),
                              tags.title.empty() ? ft.c_str() : tags.title.c_str(), tags.genre.c_str(),
                              tags.language.c_str(), tags.year, d.mtime);
                }
            }
            s_done++;
            if ((s_done & 7) == 0) vTaskDelay(1);
        }
        xSemaphoreTake(s_mutex, portMAX_DELAY);
        if (!changed && fresh.size() != s_lib.size()) changed = true;   // Folders were removed
        const bool replace = changed || s_gen.load() == 0;
        if (replace) {
            s_lib = std::move(fresh);
            s_gen++;
        }
        xSemaphoreGive(s_mutex);
        ESP_LOGI(TAG, "scan: %u folders, %u reused, changed=%d", (unsigned)dirs.size(), (unsigned)reused, (int)changed);
        if (changed) {
            std::vector<uint8_t> data;
            xSemaphoreTake(s_mutex, portMAX_DELAY);
            s_lib.encode(data);
            xSemaphoreGive(s_mutex);
            saveCache(data);
        }
    } else {
        xSemaphoreTake(s_mutex, portMAX_DELAY);
        s_gen++;
        xSemaphoreGive(s_mutex);
    }
    s_scanning = false;
    vTaskDelete(nullptr);
}

// File names found by name patterns win; the .txt tags (#AUDIO/#MP3, #INSTRUMENTAL, #VOCALS) fill in
// what is missing. A tag must name a file that exists in the folder (no paths).
void applyAudioTags(const std::string &folder, const std::vector<std::string> &files, Media &song,
                    std::string &vocals)
{
    FILE *f = fopen(song.lyrics.c_str(), "rb");
    if (!f) return;
    std::string buf(SupernovaIndex::TAG_SCAN_BYTES, '\0');
    const size_t n = fread(&buf[0], 1, buf.size(), f);
    fclose(f);
    SupernovaIndex::Tags tags;
    SupernovaIndex::parseTags(buf.data(), n, tags);
    auto resolve = [&](const std::string &name) -> std::string {
        if (name.empty() || name.find('/') != std::string::npos || name.find('\\') != std::string::npos) return "";
        for (const std::string &file : files)
            if (strcasecmp(file.c_str(), name.c_str()) == 0) return folder + "/" + file;
        return "";
    };
    if (song.instrumental.empty()) song.instrumental = resolve(tags.instrumental);
    if (vocals.empty()) vocals = resolve(tags.vocals);
    if (song.audio.empty()) {
        song.audio = resolve(tags.audio);
        if (song.audio.empty()) song.audio = resolve(tags.mp3);
    }
    if (song.instrumental == song.audio || song.instrumental == vocals) song.instrumental.clear();
}

}  // namespace

std::string folderPath(const char *folderName) { return std::string(ROOT) + "/" + folderName; }

SupernovaSync::Store &syncStore()
{
    if (s_syncLoaded) return s_sync;
    BoardSd::Guard guard;
    if (!BoardSd::mounted()) return s_sync;   // Try again once the card is there
    s_syncLoaded = true;
    if (FILE *f = fopen(SYNC, "rb")) {
        std::string data;
        if (fseek(f, 0, SEEK_END) == 0) {
            const long size = ftell(f);
            if (size > 0 && (size_t)size <= MAX_SYNC_BYTES && fseek(f, 0, SEEK_SET) == 0) {
                data.resize((size_t)size);
                if (fread(&data[0], 1, data.size(), f) != data.size()) data.clear();
            }
        }
        fclose(f);
        s_sync.decode(data.data(), data.size());
    }
    return s_sync;
}

bool saveSync()
{
    const std::string data = s_sync.encode();
    BoardSd::Guard guard;
    if (!BoardSd::mounted() || !BoardSd::ensureDir(ROOT)) return false;
    FILE *f = fopen(SYNC_TMP, "wb");
    if (!f) return false;
    const bool ok = fwrite(data.data(), 1, data.size(), f) == data.size();
    fclose(f);
    if (!ok) {
        remove(SYNC_TMP);
        return false;
    }
    remove(SYNC);
    if (rename(SYNC_TMP, SYNC) != 0) {
        ESP_LOGW(TAG, "could not store %s", SYNC);
        return false;
    }
    return true;
}

bool findMedia(const std::string &folder, Media &song)
{
    DIR *dir = opendir(folder.c_str());
    if (!dir) return false;
    std::vector<std::string> files;
    while (dirent *entry = readdir(dir)) {
        if (entry->d_name[0] == '.') continue;
        const std::string path = folder + "/" + entry->d_name;
        struct stat st {};
        if (stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode)) files.emplace_back(entry->d_name);
    }
    closedir(dir);
    std::sort(files.begin(), files.end());
    std::string avi, vocals;
    for (const std::string &file : files) {
        const std::string path = folder + "/" + file;
        if (endsWithExt(file, ".mp3")) {
            std::string low = file;
            for (char &c : low) c = (char)tolower((unsigned char)c);
            if (low.find("[kar") != std::string::npos || low.find("[inst") != std::string::npos) {
                if (song.instrumental.empty()) song.instrumental = path;
            } else if (low.find("[voc") != std::string::npos) {
                if (vocals.empty()) vocals = path;
            } else if (song.audio.empty()) {
                song.audio = path;
            }
        }
        else if (song.video.empty() && endsWithExt(file, ".mp4")) song.video = path;
        else if (avi.empty() && endsWithExt(file, ".avi")) avi = path;
        else if (song.lyrics.empty() && endsWithExt(file, ".txt")) song.lyrics = path;
    }
    if (!avi.empty()) song.video = avi;
    if (!song.lyrics.empty()) applyAudioTags(folder, files, song, vocals);
    if (song.audio.empty()) song.audio = vocals;
    return !song.video.empty() && !song.lyrics.empty();
}

void start(bool force)
{
    if (!s_mutex) s_mutex = xSemaphoreCreateMutex();
    if (!s_mutex) return;
    if (!s_loaded) {
        s_loaded = true;
        SupernovaIndex::Library cached;
        if (loadCache(cached)) {
            xSemaphoreTake(s_mutex, portMAX_DELAY);
            s_lib = std::move(cached);
            s_gen++;
            xSemaphoreGive(s_mutex);
        }
    }
    bool expected = false;
    if (!s_scanning.compare_exchange_strong(expected, true)) return;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_force = force;
    xSemaphoreGive(s_mutex);
    s_done = 0;
    s_total = 0;
    if (xTaskCreatePinnedToCore(scanTask, "snscan", 8192, nullptr, 2, nullptr, 0) != pdPASS) {
        s_scanning = false;
        ESP_LOGW(TAG, "scan task failed to start");
    }
}

bool scanning() { return s_scanning.load(); }
void progress(size_t &done, size_t &total)
{
    done = s_done.load();
    total = s_total.load();
}
uint32_t generation() { return s_gen.load(); }
bool catalogError() { return s_catalogError.load(); }

void withLibrary(const std::function<void(const SupernovaIndex::Library &)> &fn)
{
    if (!s_mutex) return;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    fn(s_lib);
    xSemaphoreGive(s_mutex);
}

}  // namespace SupernovaLibrary
