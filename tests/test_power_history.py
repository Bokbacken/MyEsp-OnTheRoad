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
#include <cassert>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>
#include <sys/stat.h>
using esp_err_t = int;
constexpr int pdPASS=1;
#define pdMS_TO_TICKS(n) (n)
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
#define ESP_LOGE(...) ((void)0)
using portMUX_TYPE = int;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(p) ((void)(p))
#define portEXIT_CRITICAL(p) ((void)(p))
uint32_t nowMs=0;
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
inline int64_t now() { return 1793500000+nowMs/1000; }
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
inline FILE *openHistory(const char *p, const char *) {
    assert(std::string(p)=="/sdcard/otr/power/power-02.csv"); return reinterpret_cast<FILE *>(1);
}
inline int writeHistory(const char *s, FILE *) {
    output.emplace_back(s); return 0;
}
inline int closeHistory(FILE *) { return nowMs==601000 ? -1 : 0; }
inline int statHistory(const char *, struct stat *s) { s->st_size=2000; return 0; }
#define fopen openHistory
#define fputs writeHistory
#define fclose closeHistory
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
    assert(PowerHistory::begin() && worker);
    try { worker(nullptr); } catch (const std::runtime_error &) {}
    auto s=PowerHistory::status();
    assert(s.running && s.written==2 && s.dropped==1);
    assert(output.size()==4); // header plus three intervals, one close failure
    auto header=fields(output[0]), row=fields(output[1]);
    assert(header.size()==40 && row.size()==header.size());
    assert(row[8]=="301000" && row[9]=="300000"); // first row after exactly five enabled minutes
    assert(row[10]=="4100" && row[11]=="4100" && row[12]=="4100");
    assert(row[15]=="100000" && row[16]=="26" && row[17]=="100000" && row[22]=="200000");
    assert(row[30]=="600000" && row[31]=="900000");
    assert(row[34]=="64000" && row[35]=="32000" && row[38]=="25.50");
    auto partial=fields(output[3]);
    assert(partial.size()==40 && partial[9]=="1000" && partial[39]=="1");
    puts("Power history: exact five-minute CSV, durations, IO deltas, partial flush and dropped rows passed");
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
