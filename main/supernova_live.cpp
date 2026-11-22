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

}  // namespace SupernovaLive
