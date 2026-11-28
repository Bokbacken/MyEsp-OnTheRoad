// Host test for SupernovaLive: version bumps and the command queue.
#include <stdio.h>

#include "supernova_live.h"

#define CHECK(cond)                                                      \
    do {                                                                 \
        if (!(cond)) {                                                   \
            fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, #cond); \
            return 1;                                                    \
        }                                                                \
    } while (0)

int main()
{
    using namespace SupernovaLive;
    Status s;
    s.state = State::PLAYING;
    s.title = "Song";
    s.posMs = 1200;
    publish(s);
    CHECK(version() == 1);

    s.posMs = 1900;   // Same second: no new version
    publish(s);
    CHECK(version() == 1);
    s.posMs = 2100;
    publish(s);
    CHECK(version() == 2);
    s.line = "Hello";
    publish(s);
    CHECK(version() == 3);
    CHECK(get().line == "Hello" && get().version == 3);

    Cmd c;
    CHECK(!pop(c));
    for (size_t i = 0; i < CMD_QUEUE; i++) {
        Cmd p;
        p.type = CmdType::SEEK;
        p.arg = (int32_t)i;
        CHECK(push(p));
    }
    Cmd full;
    CHECK(!push(full));
    CHECK(pop(c) && c.type == CmdType::SEEK && c.arg == 0);
    clearCmds();
    CHECK(!pop(c));

    WebCmd w;
    WebStatus ws;
    CHECK(!webPush(w));   // No browser has reported yet
    ws.state = State::PLAYING;
    ws.title = "Web song";
    webReport(ws, 1000);
    CHECK(webPush(w));
    WebStatus got;
    CHECK(webStatus(got, 1000 + WEB_LINK_TIMEOUT_MS) && got.title == "Web song");
    w.type = WebCmdType::PLAY;
    w.folder = "A - B";
    CHECK(webPush(w));
    WebCmd p;
    CHECK(webPop(p) && p.type == WebCmdType::STOP);
    CHECK(webPop(p) && p.type == WebCmdType::PLAY && p.folder == "A - B");
    CHECK(!webPop(p));
    CHECK(webPush(w));
    CHECK(!webStatus(got, 1000 + WEB_LINK_TIMEOUT_MS + 1));   // Stale link drops pending commands
    CHECK(!webPop(p));
    puts("OK");
    return 0;
}
