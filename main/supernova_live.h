// supernova_live.h - thread-safe hand-over between the Supernova UI (LVGL task) and the web
// remote (httpd task): the UI publishes what is playing, the web queues commands that the UI
// executes on its own task. Pure C++ (host-testable).
#pragma once

#include <stdint.h>

#include <string>

namespace SupernovaLive {

enum class State : uint8_t { OFF, BROWSE, PLAYING, PAUSED };   // OFF = Supernova is not on screen

struct Status {
    uint32_t version = 0;   // Bumped by publish() when something visible changed
    State state = State::OFF;
    std::string folder, title, artist, line, next;   // line/next: current and next lyric phrase
    uint32_t posMs = 0, durMs = 0;
    bool instrumental = false;   // Playing the version without vocals
    uint8_t volume = 0;
};

// Stores `s` and bumps the version when it differs from the current status (position counts per second).
void publish(const Status &s);
Status get();
uint32_t version();

enum class CmdType : uint8_t { PLAY, PAUSE, RESUME, SEEK, STOP, VOLUME };

struct Cmd {
    CmdType type = CmdType::STOP;
    int32_t arg = 0;        // SEEK: delta in ms, VOLUME: 0-100, PLAY: 1 = without vocals
    std::string folder;     // PLAY only
};

constexpr size_t CMD_QUEUE = 8;
bool push(const Cmd &c);   // false when the queue is full
bool pop(Cmd &out);        // false when empty
void clearCmds();

// ---- Browser player link: the device as remote for /supernova/play ----------------------------------
// The browser reports its state about once a second (webReport) and receives the queued device
// commands in the reply. The device only offers the remote while a report is fresh.
constexpr uint32_t WEB_LINK_TIMEOUT_MS = 6000;

struct WebStatus {
    State state = State::BROWSE;   // BROWSE = nothing playing in the browser
    std::string folder, title, artist;
    uint32_t posMs = 0, durMs = 0;
};

enum class WebCmdType : uint8_t { PLAY, RANDOM, LIST, PAUSE, RESUME, STOP, NEXT };

struct WebCmd {
    WebCmdType type = WebCmdType::STOP;
    uint16_t list = 0;      // LIST: playlist id
    bool shuffle = false;   // LIST
    std::string folder;     // PLAY
};

constexpr size_t WEB_CMD_QUEUE = 8;
void webReport(const WebStatus &s, uint32_t nowMs);
bool webStatus(WebStatus &out, uint32_t nowMs);   // false when no browser has reported recently
bool webPush(const WebCmd &c);                    // false when no browser is linked or the queue is full
bool webPop(WebCmd &out);

}  // namespace SupernovaLive
