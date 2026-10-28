// g++ -std=c++17 -Wall -Wextra -Imain -Icomponents/otr_engine/include -Icomponents/otr_story/include tests/test_adv_s3.cpp main/adv_loader.cpp main/adv_sd_session.cpp components/otr_engine/otr_world.cpp components/otr_story/otr_story.cpp -o tests/test_adv_s3 && ./tests/test_adv_s3
#include "adv_loader.h"
#include "adv_sd_session.h"
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

using namespace Otr;
static int passed = 0, failed = 0;
#define CHECK(c) do { if (c) ++passed; else { ++failed; std::printf("FAIL %d: %s\n", __LINE__, #c); } } while (0)
static Adv::Loaded loaded;
static World w;
static char error[128];

static bool load(const std::string &text)
{
    const bool ok = Adv::parse(text.data(), text.size(), loaded, error, sizeof(error));
    if (!ok) std::printf("parse: %s\n", error);
    return ok && w.load(loaded.map);
}

static void skates()
{
    CHECK(load("@legend\ni = ice\ns = skates\n> = belt right\n@map\nSsiii>..X\n"));
    CHECK(w.move(Dir::RIGHT) & EV_TREASURE);
    CHECK(w.hasSkates());
    MoveTrace trace;
    CHECK(w.move(Dir::RIGHT, &trace) & EV_MOVED);
    CHECK(w.playerX() == 2 && trace.count == 1 && w.steps() == 2);
    Dir direction;
    CHECK(!w.forcedMotion(direction));
    CHECK(w.move(Dir::LEFT) & EV_MOVED);
    CHECK(w.playerX() == 1);
    auto planner = std::make_unique<Planner>();
    auto path = std::make_unique<Path>();
    CHECK(planner->plan(w, 4, 0, WalkMode::ASSIST, *path) == PlanResult::OK);
    CHECK(path->len == 3);
    for (int i = 0; i < path->len; ++i) w.move((Dir)path->dirs[i]);
    CHECK(w.playerX() == 4);
    CHECK(w.move(Dir::RIGHT, &trace) & EV_MOVED);
    CHECK(w.playerX() == 6 && trace.count == 2); // Belts still move a skater.

    CHECK(load("@legend\ni = ice\ns = skates\n@map\nS#ii.\n"));
    w.setSkates(true);
    w.setPogo(1);
    CHECK(w.move(Dir::RIGHT) & EV_BLOCKED);
    CHECK(w.vault(&trace) & EV_MOVED);
    CHECK(w.playerX() == 2 && trace.count == 1);
    w.setSkates(false);
    CHECK(w.move(Dir::RIGHT, &trace) & EV_MOVED);
    CHECK(w.playerX() == 4 && trace.count == 2);

    CHECK(load("@map\nS.\n@treasures\n1 0 skates\n"));
    CHECK(w.move(Dir::RIGHT) & EV_TREASURE);
    CHECK(w.hasSkates());
    World next;
    const char *rows[] = {"S."};
    CHECK(next.load(rows, 1));
    next.carryFrom(w);
    CHECK(next.hasSkates());
    CHECK(load("@legend\ni = ice\n@map\nSi...\n@treasures\n1 0 skates\n"));
    CHECK(w.move(Dir::RIGHT, &trace) & EV_TREASURE);
    CHECK(w.hasSkates() && w.playerX() == 1 && trace.count == 1);
    CHECK(load("@legend\n> = belt right\ni = ice\n@map\nS>ii.\n"));
    w.setSkates(true);
    w.move(Dir::RIGHT, &trace);
    CHECK(w.playerX() == 2 && trace.count == 2);
}

static void hazards()
{
    for (int suit : {0, 3}) {
        CHECK(load("@legend\nt = poison\nw = swamp\ns = sharp_rocks\n@map\nStws.\n"));
        w.setSuitCharges(suit);
        const int before = w.energy();
        const uint32_t first = w.move(Dir::RIGHT);
        CHECK(w.energy() == before - (suit ? 0 : 1));
        CHECK(bool(first & EV_HURT) == !suit);
        CHECK(w.move(Dir::RIGHT) & EV_HURT);
        CHECK(w.energy() == before - (suit ? 1 : 3));
        CHECK(w.move(Dir::RIGHT) & EV_HURT);
        CHECK(w.energy() == before - (suit ? 3 : 7));
        CHECK(w.suitCharges() == suit);
    }
    CHECK(load("@legend\ns = sharp_rocks\n@rules\nlives: 1\n@map\nSQs\n"));
    w.move(Dir::RIGHT);
    w.setEnergy(1);
    CHECK((w.move(Dir::RIGHT) & (EV_DIED | EV_GAME_OVER)) == (EV_DIED | EV_GAME_OVER));
    CHECK(w.gameOver());
    w.revive();
    CHECK(w.playerX() == 1);

    CHECK(load("@legend\ns = swamp\n@map\nS#s\n"));
    w.setPogo(1);
    w.move(Dir::RIGHT);
    CHECK(w.vault() & EV_HURT);
    CHECK(w.energy() == 6 && w.steps() == 1);

    CHECK(load("@legend\n> = belt right\ns = sharp_rocks\n@map\nS>s\n"));
    MoveTrace trace;
    CHECK(w.move(Dir::RIGHT, &trace) & EV_HURT);
    CHECK(w.energy() == 4 && w.playerX() == 2 && trace.count == 2);
    w.setEnergy(1);
    w.move(Dir::LEFT);
    CHECK(w.playerX() == 0 && w.livesUsed() == 1);
}

static void boxes()
{
    const char *colors[] = {"red", "green", "blue", "yellow", "purple", "cyan"};
    for (int c = 0; c < 6; ++c) {
        const std::string legend = std::string("@legend\nb = box ") + colors[c] +
            "\np = plate " + colors[c] + "\nG = gate Z\n";
        CHECK(load(legend + "@map\nSbpG.\n@plategroup Z: 2,0\n"));
        CHECK(w.obj(1, 0) == (Obj)((int)Obj::BOX_RED + c));
        CHECK(!Planner::targetable(w, 1, 0));
        CHECK(w.move(Dir::RIGHT) & EV_PUSHED);
        CHECK(w.gateOpenForGroup(35));
        CHECK(w.obj(2, 0) == (Obj)((int)Obj::BOX_RED + c));
        CHECK(w.objAmount(2, 0) == 35);
        std::vector<uint8_t> saved(w.saveSize());
        CHECK(w.save(saved.data(), saved.size()) == saved.size());
        CHECK(w.restore(saved.data(), saved.size()));
        CHECK(w.gateOpenForGroup(35));
        CHECK(w.move(Dir::RIGHT) & EV_BLOCKED); // No pushing into a gate, even open.
        CHECK(w.hit() == EV_HIT);
        CHECK(w.clearObj(2, 0));
        CHECK(!w.gateOpenForGroup(35));

        CHECK(load(std::string("@legend\nb = box ") + colors[c] + "\np = plate " +
                   colors[(c + 1) % 6] + "\n@map\nSbp=.\n"));
        w.move(Dir::RIGHT);
        CHECK(!w.gatesOpen());
    }
    CHECK(load("@legend\nb = box red\n@map\nSb....\n@treasures\n1 0 skates\n"));
    const uint32_t pushed = w.move(Dir::RIGHT);
    CHECK((pushed & (EV_PUSHED | EV_TREASURE)) == (EV_PUSHED | EV_TREASURE));
    CHECK(w.hasSkates() && w.hidden(1, 0) == Obj::NONE);
    CHECK(w.obj(2, 0) == Obj::BOX_RED);
    CHECK(load("@legend\nb = box red\n@map\nSb....\n@treasures\n1 0 gem\n"));
    CHECK(w.move(Dir::RIGHT) & EV_GEM);
    CHECK(w.gems() == 1 && w.gemsTotal() == 1);
    CHECK(w.move(Dir::RIGHT) & EV_PUSHED);
    CHECK(w.gems() == 1 && w.hidden(1, 0) == Obj::NONE);
    CHECK(load("@legend\nb = box red\n@map\nSbo=.\n"));
    w.move(Dir::RIGHT);
    CHECK(!w.gatesOpen()); // The legacy uncolored plate still requires a rock.
    CHECK(load("@map\nSRo=.\n"));
    CHECK(w.move(Dir::RIGHT) & EV_PUSHED);
    CHECK(w.gatesOpen());
    CHECK(load("@legend\np = plate red\n@map\nSRp=.\n"));
    w.move(Dir::RIGHT);
    CHECK(!w.gatesOpen()); // An old rock has no color.
    for (char terrain : {'~', '^', 'R', '#', 'Q', 'X'}) {
        CHECK(load(std::string("@legend\nb = box red\n@map\nSb") + terrain + ".\n"));
        CHECK(w.move(Dir::RIGHT) & EV_BLOCKED);
        CHECK(w.obj(1, 0) == Obj::BOX_RED && w.playerX() == 0);
    }
}

static void andGates()
{
    CHECK(load("@legend\nv = lever A\np = plate red\nb = box red\nG = gate A\n"
               "@andgate A\n@map\nv....\nSbpG.\n@plategroup A: 2,1\n"));
    w.move(Dir::UP);
    CHECK(w.activate() & EV_LEVER);
    CHECK(w.leverOnAt(0, 0) && !w.gateOpenForGroup(10));
    CHECK(w.move(Dir::RIGHT) & EV_PUSHED);
    CHECK(w.gateOpenForGroup(10));
    std::vector<uint8_t> saved(w.saveSize());
    CHECK(w.save(saved.data(), saved.size()) == saved.size());
    CHECK(w.restore(saved.data(), saved.size()));
    CHECK(w.gateOpenForGroup(10) && w.leverOnAt(0, 0));
    CHECK(w.clearObj(2, 1));
    CHECK(!w.gateOpenForGroup(10) && w.leverOnAt(0, 0));
    w.move(Dir::LEFT);
    w.move(Dir::UP);
    CHECK(w.activate() & EV_LEVER);
    CHECK(!w.leverOnAt(0, 0));

    CHECK(load("@legend\nv = lever 0\n@andgate 0\n@map\nSv=\n"));
    w.move(Dir::RIGHT);
    w.activate();
    CHECK(w.leverOnAt(1, 0) && !w.gatesOpen()); // No plates is not vacuously true.
    CHECK(load("@andgate 0\n@map\nSRo=.\n"));
    w.move(Dir::RIGHT);
    CHECK(!w.gatesOpen()); // No lever remains closed.
    CHECK(load("@legend\nv = lever 0\n@map\nSv=\n"));
    w.move(Dir::RIGHT);
    w.activate();
    CHECK(w.gatesOpen()); // Legacy latch-only behavior.
    CHECK(load("@legend\nv = lever A\np = plate A red\nb = box red\nG = gate A\n"
               "@group A: and\n@map\nv....\nSbpG.\n"));
    w.move(Dir::UP);
    w.activate();
    CHECK(!w.gateOpenForGroup(10) && w.leverOnAt(0, 0));
    w.move(Dir::RIGHT);
    CHECK(w.gateOpenForGroup(10) && w.objAmount(2, 1) == 10);
}

static uint32_t crc32(const uint8_t *p, size_t n)
{
    uint32_t crc = ~0u;
    while (n--) {
        crc ^= *p++;
        for (int i = 0; i < 8; ++i) crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}
static void fixCrc(std::vector<uint8_t> &data)
{
    const uint32_t body = (uint32_t)(data.size() - 20);
    for (int i = 0; i < 4; ++i) data[10 + i] = (uint8_t)(body >> (8 * i));
    const uint32_t crc = crc32(data.data(), data.size() - 4);
    for (int i = 0; i < 4; ++i) data[data.size() - 4 + i] = (uint8_t)(crc >> (8 * i));
}
static void saves()
{
    CHECK(load("@map\nS..\n"));
    w.setSkates(true);
    std::vector<uint8_t> saved(w.saveSize());
    CHECK(w.save(saved.data(), saved.size()) == saved.size());
    w.setSkates(false);
    CHECK(w.restore(saved.data(), saved.size()) && w.hasSkates());
    for (int fault = 0; fault < 4; ++fault) {
        auto bad = saved;
        if (fault == 0) bad[16 + 32 + 7 * w.width() * w.height() +
                            6 + 8 + 2 * PLATE_GROUPS] = 2;
        if (fault == 1) bad[48] = (uint8_t)Tile::COUNT;
        if (fault == 2) bad[49] = (uint8_t)Obj::COUNT;
        if (fault == 3) bad[51] = (uint8_t)Obj::COUNT;
        fixCrc(bad);
        CHECK(!w.restore(bad.data(), bad.size()));
        std::vector<uint8_t> unchanged(w.saveSize());
        CHECK(w.save(unchanged.data(), unchanged.size()) == unchanged.size());
        CHECK(unchanged == saved);
    }
    for (int version = 1; version <= 5; ++version) {
        auto old = saved;
        const size_t legacySize = 16 + 32 + 7 * size_t(w.width()) * size_t(w.height()) +
                                  (version >= 4 ? 6 + 8 + 2 * PLATE_GROUPS : 0) +
                                  (version >= 5 ? 1 : 0) + 4;
        old.resize(legacySize);
        old[4] = (uint8_t)version;
        fixCrc(old);
        CHECK(w.restore(old.data(), old.size()));
        CHECK(w.hasSkates() == (version >= 5));
    }
}

static void errors()
{
    struct Error { const char *text; int line; const char *part; };
    for (const Error &e : {
        Error{"@legend\nb = box orange\n@map\nSb\n", 2, "color"},
        Error{"@legend\np = plate orange\n@map\nSp\n", 2, "group"},
        Error{"@legend\ns = skates extra\n@map\nSs\n", 2, "argument"},
        Error{"@legend\ns = poison extra\n@map\nSs\n", 2, "argument"},
        Error{"@andgate\n@map\nS.\n", 1, "group"},
        Error{"@andgate 36\n@map\nS.\n", 1, "group"},
        Error{"@andgate A\n@andgate a\n@map\nS.\n", 2, "conflicting"},
        Error{"@andgate A\n@timedgate A: 3\n@map\nS.\n", 2, "conflicting"},
        Error{"@timedgate A: 3\n@andgate A\n@map\nS.\n", 2, "conflicting"},
        Error{"@legend\np = plate red\n@map\nSp\n@treasures\n1 0 gem\n", 6, "share"},
        Error{"@legend\nb = box red\n@map\nSb\n@plategroup A: 1,0\n", 5, "not a plate"},
        Error{"@legend\ns = skates\ns = box red\n@map\nSs\n", 3, "duplicate"},
        Error{"@legend\nv = lever 0\np = plate red\n@map\nSvp=\n", 5, "mix"},
        Error{"@group A: or\n@map\nS.\n", 1, "and"},
        Error{"@legend\np = plate 36 red\n@map\nSp\n", 2, "group"},
        Error{"@legend\np = plate A orange\n@map\nSp\n", 2, "color"}
    }) {
        CHECK(!Adv::parse(e.text, std::strlen(e.text), loaded, error, sizeof(error)));
        const std::string prefix = "line " + std::to_string(e.line) + ":";
        CHECK(std::strncmp(error, prefix.c_str(), prefix.size()) == 0);
        CHECK(std::strstr(error, e.part));
    }
    // Legend/resource priority and built-in overriding remain unchanged.
    CHECK(load("@resources\ns : stone : Stone\n@legend\ns = skates\n@map\nSs\n"));
    CHECK(w.move(Dir::RIGHT) & EV_TREASURE);
    CHECK(w.hasSkates() && loaded.placementCount == 0);
    CHECK(load("@legend\nR = box cyan\n@map\nSR..\n"));
    CHECK(w.obj(1, 0) == Obj::BOX_CYAN);
}

static void sdSession()
{
    auto policyDoc = std::make_unique<Adv::Loaded>();
    Adv::CatalogEntry entry{};
    std::strcpy(entry.folder, "josef");
    entry.mapCount = 2;
    std::strcpy(entry.maps[0], "01_lager.adv");
    std::strcpy(entry.maps[1], "02_vag.adv");
    Otr::Story::StoryState story;
    const Otr::Story::Script *script = nullptr;
    Adv::SdSession session;
    for (int policy = 0; policy < 4; ++policy) {
        CHECK(session.start("sdcard_bundle/otr/adventure", entry, loaded, w, story, script,
                            error, sizeof(error)));
        CHECK(!w.hasSkates());
        w.setSkates(true);
        const std::string rules = std::string("@rules\n") +
            (policy & 1 ? "carry: energy suit keys skates\n" : "") +
            (policy & 2 ? "reset: energy suit keys skates\n" : "") + "@map\nS.\n";
        CHECK(Adv::parse(rules.data(), rules.size(), *policyDoc, error, sizeof(error)));
        loaded.carryMask = policyDoc->carryMask;
        loaded.resetMask = policyDoc->resetMask;
        CHECK(session.travel(loaded, w, story, script, error, sizeof(error)));
        CHECK(w.hasSkates()); // Permanent equipment is independent of carry/reset.
        std::vector<uint8_t> saved(w.saveSize() + Otr::Story::StoryState::SAVE_BYTES + 56);
        CHECK(session.save(w, story, saved.data(), saved.size()) == saved.size());
        CHECK(session.restart(loaded, w, story, script, error, sizeof(error)));
        CHECK(!w.hasSkates());
        CHECK(session.restore(saved.data(), saved.size(), loaded, w, story, script, error, sizeof(error)));
        CHECK(w.hasSkates());
    }
}

int main()
{
    skates(); hazards(); boxes(); andGates(); saves(); errors(); sdSession();
    std::printf("%s: %d passed, %d failed\n", failed ? "FAIL" : "OK", passed, failed);
    return failed ? 1 : 0;
}
