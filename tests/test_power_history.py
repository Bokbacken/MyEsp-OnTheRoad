#!/usr/bin/env python3
"""Exercise the production history scheduler and CSV writer with deterministic hardware doubles."""
import pathlib
import re
import subprocess
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
STUB = r'''
#include "power_history.h"
#include "power_interval.h"
#include "power_io.h"
#include <algorithm>
#include <cassert>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <stdexcept>
#include <string>
#include <time.h>
#include <vector>
#include <sys/stat.h>
using esp_err_t = int;
constexpr int pdPASS=1;
#define pdMS_TO_TICKS(n) (n)
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
#define ESP_LOGE(tag, ...) do { if (false) std::fprintf(stderr, __VA_ARGS__); } while (0)
using portMUX_TYPE = int;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(p) ((void)(p))
#define portEXIT_CRITICAL(p) ((void)(p))
uint32_t nowMs=0;
int64_t baseEpoch=0;
void (*worker)(void *)=nullptr;
inline int xTaskCreate(void (*f)(void *), const char *, int, void *, int, void *) { worker=f; return pdPASS; }
inline void vTaskDelay(int ms) {
    nowMs += ms;
    if (nowMs >= 603000) throw std::runtime_error("done");
}
struct AppConfig { bool powerLog=true; uint16_t batteryScalePermille=2000; uint8_t sfxVol=60, musicVol=25; };
namespace Config { inline AppConfig get() { AppConfig c; c.powerLog=nowMs<602000; return c; } }
namespace MySys { inline uint32_t millis() { return nowMs; } }
namespace MyDiag { inline uint32_t bootCount() { return 18; } }
inline int esp_reset_reason() { return 3; }
namespace MyTime {
inline int64_t now() { return baseEpoch+nowMs/1000; }
inline unsigned source() { return 4; }
inline bool certain() { return true; }
}
namespace MyRtc {
inline bool temperatureAvailable() { return true; }
inline bool readTemp(float &c) { c=25.5; return true; }
}
namespace HwProfile {
enum class Id { P4_43 };
struct Info { Id id=Id::P4_43; };
inline Info active() { return {}; }
}
namespace BoardSd {
struct Guard { Guard() {} ~Guard() {} };
struct Info { uint32_t errors=0; };
inline Info info() { return {}; }
inline bool transferHealthy() { return true; }
inline bool ensureDir(const char *) { return true; }
}
namespace Audio {
struct Status { uint32_t underruns=0; bool ampOn=true; };
inline Status status() { return {}; }
}
namespace MyWifi {
struct Info { bool connected=true; int rssi=-55; };
inline Info info() { return {}; }
inline bool connected() { return nowMs<101000; }
}
namespace BoardBattery {
struct Status { bool supported=true, sampleValid=true, adcCalibrated=true; uint16_t adcMillivolts=2050; };
inline Status status() { return {}; }
}
namespace BatteryMath {
inline bool batteryMillivolts(uint16_t adc, uint16_t scale, uint16_t &mv) { mv=uint32_t(adc)*scale/1000; return true; }
}
namespace GameLive {
struct Snapshot { bool active=true; unsigned gameType=0; };
inline void get(Snapshot &) {}
inline bool started(const Snapshot &) { return true; }
inline bool finished(const Snapshot &) { return false; }
}
namespace AdvLive { inline bool active() { return false; } }
namespace Karaoke { inline bool playing() { return false; } }
namespace Backlight {
inline bool sleeping() { return nowMs>=101000; }
inline uint8_t get() { return 80; }
}
namespace UiScreenPower { inline bool ecoSleeping() { return nowMs>=101000; } }
constexpr int MALLOC_CAP_INTERNAL=0;
inline size_t heap_caps_get_free_size(int) { return 64000; }
inline size_t heap_caps_get_largest_free_block(int) { return 32000; }
namespace PowerIo {
inline Counts counts() { return {nowMs*2, nowMs*3, nowMs*4, nowMs*5}; }
}
std::vector<std::string> output;
std::vector<std::string> files, removedFiles, openedPaths;
size_t directoryIndex=0;
bool failList=false, failRead=false, failStat=false, failRemove=false, failOpen=false;
std::string directoryName, emptyName;
inline std::string historyBasename(const char *path) {
    const char *slash=strrchr(path, '/');
    return slash ? slash+1 : path;
}
inline DIR *openHistoryDir(const char *path) {
    assert(std::string(path)=="/sdcard/otr/power");
    if (failList) { errno=EIO; return nullptr; }
    directoryIndex=0;
    return reinterpret_cast<DIR *>(1);
}
inline struct dirent *readHistoryDir(DIR *) {
    static struct dirent entry{};
    if (failRead) { errno=EIO; return nullptr; }
    if (directoryIndex>=files.size()) return nullptr;
    std::memset(&entry, 0, sizeof(entry));
    std::snprintf(entry.d_name, sizeof(entry.d_name), "%s", files[directoryIndex++].c_str());
    return &entry;
}
inline int closeHistoryDir(DIR *) { return 0; }
inline int statHistory(const char *path, struct stat *s) {
    const auto name=historyBasename(path);
    if (failStat) { errno=EIO; return -1; }
    auto it=std::find(files.begin(), files.end(), name);
    if (it==files.end()) { errno=ENOENT; return -1; }
    s->st_mode=name==directoryName ? S_IFDIR : S_IFREG;
    s->st_size=name==emptyName ? 0 : 2*1024*1024;
    s->st_mtime=std::distance(files.begin(), it);
    return 0;
}
inline int removeHistory(const char *path) {
    if (failRemove) { errno=EIO; return -1; }
    const auto name=historyBasename(path);
    auto it=std::find(files.begin(), files.end(), name);
    if (it==files.end()) { errno=ENOENT; return -1; }
    removedFiles.push_back(name);
    files.erase(it);
    return 0;
}
inline FILE *openHistory(const char *p, const char *mode) {
    assert(std::string(mode)=="a");
    if (failOpen) { errno=EIO; return nullptr; }
    openedPaths.emplace_back(p);
    const auto name=historyBasename(p);
    if (std::find(files.begin(), files.end(), name)==files.end()) files.push_back(name);
    return reinterpret_cast<FILE *>(1);
}
inline int writeHistory(const char *s, FILE *) {
    output.emplace_back(s); return 0;
}
inline int closeHistory(FILE *) { return nowMs==601000 ? -1 : 0; }
#define fopen openHistory
#define fputs writeHistory
#define fclose closeHistory
#define opendir openHistoryDir
#define readdir readHistoryDir
#define closedir closeHistoryDir
#define remove removeHistory
#define stat(...) statHistory(__VA_ARGS__)
#define FW_VERSION "test"
'''
TEST = r'''
std::vector<std::string> fields(const std::string &line) {
    std::vector<std::string> out;
    size_t start=0;
    for (size_t i=0; i<line.size(); ++i)
        if (line[i]==',' || line[i]=='\n') { out.push_back(line.substr(start,i-start)); start=i+1; }
    return out;
}
int main() {
    struct tm start{};
    start.tm_year=126; start.tm_mon=10; start.tm_mday=2; start.tm_hour=23; start.tm_min=54;
    baseEpoch=timegm(&start);
    for (unsigned i=0; i<150; ++i) {
        char name[32];
        std::snprintf(name, sizeof(name), "power-%08u.csv", 20260101+i);
        files.emplace_back(name);
    }
    assert(PowerHistory::begin() && worker);
    try { worker(nullptr); } catch (const std::runtime_error &) {}
    auto s=PowerHistory::status();
    assert(s.running && s.written==2 && s.dropped==1);
    assert(output.size()==5); // two daily headers, three intervals, one close failure
    assert(openedPaths.size()==3);
    assert(openedPaths[0].find("power-20261102.csv")!=std::string::npos);
    assert(openedPaths[1].find("power-20261103.csv")!=std::string::npos);
    assert(openedPaths[2].find("power-20261103.csv")!=std::string::npos);
    assert(files.size()==150 && removedFiles.size()==2);
    assert(removedFiles[0]=="power-20260101.csv");
    assert(removedFiles[1]=="power-20260102.csv");
    auto header=fields(output[0]), row=fields(output[1]);
    assert(header.size()==40 && row.size()==header.size());
    assert(row[8]=="301000" && row[9]=="300000"); // first row after exactly five enabled minutes
    assert(row[10]=="4100" && row[11]=="4100" && row[12]=="4100");
    assert(row[15]=="100000" && row[16]=="26" && row[17]=="100000" && row[22]=="200000");
    assert(row[30]=="600000" && row[31]=="900000");
    assert(row[34]=="64000" && row[35]=="32000" && row[38]=="25.50");
    auto partial=fields(output[4]);
    assert(partial.size()==40 && partial[9]=="1000" && partial[39]=="1");

    char path[64];
    assert(PowerHistory::makePath(path, sizeof(path), baseEpoch, false));
    assert(std::string(path)=="/sdcard/otr/power/power-boot00000018.csv");
    assert(PowerHistory::makePath(path, sizeof(path), 0, true));
    assert(std::string(path)=="/sdcard/otr/power/power-boot00000018.csv");
    assert(PowerHistory::makePath(path, sizeof(path), baseEpoch, true));
    assert(std::string(path)=="/sdcard/otr/power/power-20261102.csv");
    assert(!PowerHistory::makePath(path, 8, baseEpoch, true));

    for (const char *name : {"power-00.csv", "power-20261102.csv",
                            "power-boot00000018.csv", "power-boot4294967295.csv"})
        assert(PowerHistory::isPowerFile(name));
    for (const char *name : {"notes.csv", "power-notes.csv", "power-1.csv",
                            "power-20261102.csv.bak", "power-bootBAD00018.csv"})
        assert(!PowerHistory::isPowerFile(name));

    files={"power-20261102.csv"};
    output.clear(); openedPaths.clear(); removedFiles.clear();
    assert(PowerHistory::append("continued\n", baseEpoch, true));
    assert(output.size()==1 && output[0]=="continued\n");
    assert(files.size()==1 && removedFiles.empty()); // No truncation or duplicate header after restart.
    emptyName=files[0];
    output.clear();
    assert(PowerHistory::append("empty\n", baseEpoch, true));
    assert(output.size()==2 && output[0]==PowerHistory::HEADER);
    emptyName.clear();

    files.clear(); output.clear();
    assert(PowerHistory::append("uncertain\n", baseEpoch, false));
    assert(openedPaths.back()=="/sdcard/otr/power/power-boot00000018.csv");
    assert(PowerHistory::append("certain\n", baseEpoch, true));
    assert(openedPaths.back()=="/sdcard/otr/power/power-20261102.csv");
    assert(files.size()==2 && output.size()==4);

    files={"power-00.csv", "notes.csv", "power-notes.csv", "power-20200101.csv"};
    directoryName="power-20200101.csv";
    for (unsigned i=1; i<150; ++i) {
        char name[32];
        std::snprintf(name, sizeof(name), "power-boot%08u.csv", i);
        files.emplace_back(name);
    }
    removedFiles.clear();
    assert(PowerHistory::append("pruned\n", baseEpoch, true));
    assert(removedFiles.size()==1 && removedFiles[0]=="power-00.csv");
    assert(files.size()==153); // 150 logs, two unrelated files, one directory.
    assert(std::find(files.begin(), files.end(), directoryName)!=files.end());
    directoryName.clear();

    files.clear();
    const auto writes=openedPaths.size();
    const auto dropped=PowerHistory::status().dropped;
    failList=true;
    assert(!PowerHistory::append("list failure\n", baseEpoch, true));
    failList=false; failRead=true;
    assert(!PowerHistory::append("read failure\n", baseEpoch, true));
    failRead=false; failStat=true;
    assert(!PowerHistory::append("stat failure\n", baseEpoch, true));
    failStat=false; failOpen=true;
    assert(!PowerHistory::append("open failure\n", baseEpoch, true));
    failOpen=false;
    for (unsigned i=0; i<150; ++i) {
        char name[32];
        std::snprintf(name, sizeof(name), "power-boot%08u.csv", i);
        files.emplace_back(name);
    }
    failRemove=true;
    assert(!PowerHistory::append("prune failure\n", baseEpoch, true));
    failRemove=false;
    assert(files.size()==150 && openedPaths.size()==writes);
    assert(PowerHistory::status().dropped==dropped+5);
    puts("Power history: daily files, 150-file retention, CSV metrics, partial flush and dropped rows passed");
}
'''

with tempfile.TemporaryDirectory(prefix="otr-power-history-") as directory:
    work = pathlib.Path(directory)
    source = (ROOT / "main/power_history.cpp").read_text()
    source = re.sub(r'^#include "[^"]+"\n', "", source, flags=re.MULTILINE)
    (work / "history.cpp").write_text(STUB + source + TEST)
    binary = work / "test"
    subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
                    f"-I{ROOT / 'main'}", str(work / "history.cpp"), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
