// supernova_live.cpp - see supernova_live.h.
#include "supernova_live.h"

#include <deque>
#include <mutex>

namespace SupernovaLive {

namespace {

std::mutex s_mutex;
Status s_status;
std::deque<Cmd> s_cmds;

bool sameVisible(const Status &a, const Status &b)
{
    return a.state == b.state && a.folder == b.folder && a.title == b.title && a.artist == b.artist &&
           a.line == b.line && a.next == b.next && a.posMs / 1000 == b.posMs / 1000 && a.durMs == b.durMs &&
           a.instrumental == b.instrumental && a.volume == b.volume;
}

}  // namespace

void publish(const Status &s)
{
    std::lock_guard<std::mutex> lock(s_mutex);
    if (sameVisible(s_status, s)) return;
    const uint32_t next = s_status.version + 1;
    s_status = s;
    s_status.version = next;
}

Status get()
{
    std::lock_guard<std::mutex> lock(s_mutex);
    return s_status;
}

uint32_t version()
{
    std::lock_guard<std::mutex> lock(s_mutex);
    return s_status.version;
}

bool push(const Cmd &c)
{
    std::lock_guard<std::mutex> lock(s_mutex);
    if (s_cmds.size() >= CMD_QUEUE) return false;
    s_cmds.push_back(c);
    return true;
}

bool pop(Cmd &out)
{
    std::lock_guard<std::mutex> lock(s_mutex);
    if (s_cmds.empty()) return false;
    out = std::move(s_cmds.front());
    s_cmds.pop_front();
    return true;
}

void clearCmds()
{
    std::lock_guard<std::mutex> lock(s_mutex);
    s_cmds.clear();
}

namespace {
WebStatus s_web;
std::deque<WebCmd> s_webCmds;
bool s_webSeen = false;
uint32_t s_webAt = 0;

bool webFresh(uint32_t nowMs) { return s_webSeen && (uint32_t)(nowMs - s_webAt) <= WEB_LINK_TIMEOUT_MS; }
}  // namespace

void webReport(const WebStatus &s, uint32_t nowMs)
{
    std::lock_guard<std::mutex> lock(s_mutex);
    s_web = s;
    s_webSeen = true;
    s_webAt = nowMs;
}

bool webStatus(WebStatus &out, uint32_t nowMs)
{
    std::lock_guard<std::mutex> lock(s_mutex);
    if (!webFresh(nowMs)) {
        s_webCmds.clear();
        return false;
    }
    out = s_web;
    return true;
}

bool webPush(const WebCmd &c)
{
    std::lock_guard<std::mutex> lock(s_mutex);
    if (!s_webSeen || s_webCmds.size() >= WEB_CMD_QUEUE) return false;
    s_webCmds.push_back(c);
    return true;
}

bool webPop(WebCmd &out)
{
    std::lock_guard<std::mutex> lock(s_mutex);
    if (s_webCmds.empty()) return false;
    out = std::move(s_webCmds.front());
    s_webCmds.pop_front();
    return true;
}

}  // namespace SupernovaLive
