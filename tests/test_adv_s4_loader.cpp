// g++ -std=c++17 -Wall -Wextra -Imain -Icomponents/otr_engine/include -Icomponents/otr_story/include tests/test_adv_s4_loader.cpp main/adv_loader.cpp main/adv_sd_session.cpp components/otr_engine/otr_world.cpp components/otr_story/otr_story.cpp -o tests/test_adv_s4_loader && ./tests/test_adv_s4_loader
#include "adv_loader.h"
#include "adv_sd_session.h"
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace Otr;
static Adv::Loaded loaded;
static World world;
static char error[160];
static int passed, failed;
#define CHECK(c) do { if (c) ++passed; else { ++failed; std::printf("FAIL %d: %s (%s)\n", __LINE__, #c, error); } } while (0)

static bool parse(const std::string &text)
{
    error[0] = 0;
    return Adv::parse(text.data(), text.size(), loaded, error, sizeof(error));
}

static bool load(const std::string &text)
{
    return parse(text) && world.load(loaded.map);
}

static void bad(const std::string &text, int line, const char *message)
{
    CHECK(!parse(text));
    char prefix[32];
    std::snprintf(prefix, sizeof(prefix), "line %d:", line);
    CHECK(std::strncmp(error, prefix, std::strlen(prefix)) == 0);
    CHECK(std::strstr(error, message) != nullptr);
}

static void equipment()
{
    CHECK(load("@legend\ns = suction\n@map\nSs...\n"));
    CHECK(loaded.rows[0][1] == SUCTION_GLYPH);
    CHECK(world.obj(1, 0) == Obj::SUCTION);
    CHECK(!world.hasSuction());
    CHECK(world.move(Dir::RIGHT) & EV_TREASURE);
    CHECK(world.hasSuction());
    CHECK(load("@map\nS...\n@treasures\n1 0 suction\n"));
    CHECK(loaded.treasures[0].item == Obj::SUCTION);
    CHECK(world.move(Dir::RIGHT) & EV_TREASURE);
    CHECK(world.hasSuction());
    CHECK(load("@legend\nb = box red\n@map\nSb...\n@treasures\n1 0 suction\n"));
    CHECK(world.move(Dir::RIGHT) & EV_TREASURE);
    CHECK(world.hasSuction());
    CHECK(load("@map\nSRp*X\n"));
    CHECK(!world.hasSuction() && world.enemyCount() == 0);
    CHECK(loaded.enemyCount == 0 && loaded.map.enemies == nullptr);
    CHECK(loaded.map.enemyCount == 0);
    CHECK(load("@map\nS.\n@treasures\n1,0 potion 0\n"));
    CHECK(loaded.treasures[0].amount == 0); // Legacy zero selects the engine's default amount.
    bad("@legend\ns = suction extra\n@map\nSs\n", 2, "argument");
    bad("@map\nS.\n@treasures\n256 0 suction\n", 4, "coordinates");
    bad("@map\nS.\n@treasures\n-1 0 suction\n", 4, "coordinates");
    bad("@map\nS.\n@treasures\n1 256 suction\n", 4, "coordinates");
    bad("@map\nS.\n@treasures\n1 nope suction\n", 4, "coordinates");
    bad("@map\nS.\n@treasures\n2 0 suction\n", 4, "outside map");
    bad("@map\nS.\n@treasures\n1 0 suction 256\n", 4, "amount");
    bad("@map\nS.\n@treasures\n1 0 suction 1 extra\n", 4, "treasure needs");
}

static void definitions()
{
    CHECK(load("@enemies\npatrol 2 0 loop route 2,0 5,0\n"
               "patrol 3 1 pingpong area 1 1 5 1 route 1,1 5,1\n"
               "chaser 6 1 area 0 0 6 1\n@map\nS......\n.......\n"));
    CHECK(loaded.enemyCount == 3 && loaded.map.enemyCount == 3);
    CHECK(loaded.map.enemies == loaded.enemies);
    CHECK(world.enemyCount() == 3);
    CHECK(world.enemyDef(0).kind == EnemyKind::PATROL);
    CHECK(!world.enemyDef(0).pingPong && !world.enemyDef(0).bounded);
    CHECK(world.enemyDef(0).routeCount == 2);
    CHECK(world.enemyDef(0).route[1].x == 5);
    CHECK(world.enemyDef(1).pingPong && world.enemyDef(1).bounded);
    CHECK(world.enemyDef(1).x0 == 1 && world.enemyDef(1).x1 == 5);
    CHECK(world.enemyDef(2).kind == EnemyKind::CHASER);
    CHECK(world.enemyDef(2).routeCount == 0);
    CHECK(world.enemy(0).x == 2 && world.enemy(0).y == 0);
    CHECK(world.enemyAt(3, 1) == 1);
    std::vector<uint8_t> image(world.saveSize());
    CHECK(world.save(image.data(), image.size()) == image.size());
    world.move(Dir::RIGHT);
    CHECK(world.restore(image.data(), image.size()));
    CHECK(world.enemyCount() == 3 && world.enemyAt(3, 1) == 1);
    CHECK(world.enemyDef(1).pingPong && world.enemyDef(1).routeCount == 2);
    CHECK(load("@map\nS........\n@enemies\nchaser 8 0 # bounded by map\n"));
    CHECK(load("@resources\ns : stone : Stone\n@map\nSs...\n@enemies\nchaser 1 0\n"));
    CHECK(loaded.placementCount == 1 && world.enemyAt(1, 0) == 0);
    CHECK(load("@legend\ns = suction\n@map\nSs...\n@enemies\nchaser 1 0\n"));
    CHECK(load("@map\nS..QX\n@enemies\npatrol 1 0 loop route 0,0 3,0 4,0\n"));
    CHECK(load("@map\nS.....\n......\n......\n@enemies\n"
               "patrol 1 0 loop route 1,0 5,0 5,2 1,2\n"));
}

static void limits()
{
    const std::string map = "@map\nS....................\n";
    std::string enemies = "@enemies\n";
    for (int i = 0; i < MAX_ENEMIES; ++i)
        enemies += "chaser " + std::to_string(i + 1) + " 0\n";
    CHECK(load(map + enemies));
    CHECK(loaded.enemyCount == MAX_ENEMIES && world.enemyCount() == MAX_ENEMIES);
    bad(map + enemies + "chaser 9 0\n", 12, "too many enemies");
    std::string route = map + "@enemies\npatrol 1 0 pingpong area 0 0 20 0 route";
    for (int i = 0; i < MAX_ROUTE; ++i) route += " " + std::to_string(i + 1) + ",0";
    CHECK(load(route + "\n"));
    CHECK(world.enemyDef(0).routeCount == MAX_ROUTE);
    bad(route + " 17,0\n", 4, "too many");
    std::string edgeMap = "@map\n";
    for (int y = 0; y < MAX_H; ++y) {
        std::string row(MAX_W, '.');
        if (!y) row[0] = 'S';
        edgeMap += row + "\n";
    }
    CHECK(load(edgeMap + "@enemies\nchaser " + std::to_string(MAX_W - 1) + " " +
               std::to_string(MAX_H - 1) + " area 0 0 " +
               std::to_string(MAX_W - 1) + " " + std::to_string(MAX_H - 1) + "\n"));
}

static void blockedPatrol()
{
    CHECK(load("@map\nS.....\n.R....\n......\n@enemies\n"
               "patrol 0 1 pingpong route 0,1 3,1\n"));
    CHECK(world.move(Dir::RIGHT) & EV_MOVED);
    CHECK(world.enemy(0).x == 0 && world.enemy(0).y == 1); // No detour through row 2.
    CHECK(world.clearObj(1, 1));
    CHECK(world.move(Dir::RIGHT) & EV_MOVED);
    CHECK(world.enemy(0).x == 1 && world.enemy(0).y == 1);
    CHECK(load("@map\nS.R...\n@enemies\npatrol 1 0 pingpong route 1,0 2,0\n"));
    CHECK(load("@map\nS.1...\n@enemies\npatrol 1 0 pingpong route 1,0 2,0\n"));
}

static void invalid()
{
    const std::string map = "@map\nS.....\n......\n@enemies\n";
    struct Case { const char *line; const char *message; };
    const Case cases[] = {
        {"unknown 1 0", "kind"}, {"chaser", "enemy needs"},
        {"chaser -1 0", "coordinates"}, {"chaser 256 0", "coordinates"},
        {"chaser 999999999999999999999 0", "coordinates"}, {"chaser 1x 0", "coordinates"},
        {"chaser 6 0", "outside map"}, {"chaser 1 2", "outside map"},
        {"chaser 0 0", "overlaps player"}, {"chaser 1 0 extra", "arguments"},
        {"chaser 1 0 route 1,0", "arguments"},
        {"chaser 1 0 area 2 0 1 0", "bounds"},
        {"chaser 1 0 area 0 1 5 0", "bounds"},
        {"chaser 1 0 area 0 0 6 1", "bounds"},
        {"chaser 1 0 area 0 0 5 2", "bounds"},
        {"chaser 1 0 area 0 0 -1 1", "four coordinates"},
        {"chaser 1 0 area 0 0 256 1", "four coordinates"},
        {"chaser 1 0 area 0 0 5", "four coordinates"},
        {"chaser 1 0 area 2 0 5 1", "outside area"},
        {"patrol 1 0", "mode"}, {"patrol 1 0 circular route 1,0", "mode"},
        {"patrol 1 0 loop", "needs route"}, {"patrol 1 0 loop route", "needs route"},
        {"patrol 1 0 loop route 1", "coordinate"},
        {"patrol 1 0 loop route 1,0,0", "coordinate"},
        {"patrol 1 0 loop route -1,0", "coordinate"},
        {"patrol 1 0 loop route 9999999999999,0", "coordinate"},
        {"patrol 1 0 loop route 6,0", "outside map"},
        {"patrol 1 0 loop area 1 0 5 1 route 0,0", "outside area"},
        {"patrol 1 0 loop route 1,0 area 0 0 5 1", "coordinate"},
    };
    for (const Case &c : cases) bad(map + c.line + "\n", 5, c.message);
    bad(map + "chaser 1 0\nchaser 1 0\n", 6, "overlap");
    bad("@enemies chaser 1 0\n@map\nS.\n", 1, "no arguments");
    for (char cell : std::string("#%T~^RrcCDbm123456789=")) {
        const std::string blocked = std::string("@map\nS.") + cell + "..\n@enemies\n";
        bad(blocked + "chaser 2 0\n", 4, "walkable");
        if (std::strchr("#%T~^m", cell))
            bad(blocked + "patrol 1 0 loop route 2,0\n", 4, "walkable");
        else
            CHECK(load(blocked + "patrol 1 0 loop route 1,0 4,0\n"));
    }
    for (const char *kind : {"box red", "door red", "poison", "swamp", "sharp_rocks", "lever A", "gate A"}) {
        const std::string blocked = std::string("@legend\nv = ") + kind +
            "\n@map\nS.v..\n@enemies\n";
        bad(blocked + "chaser 2 0\n", 6, "walkable");
        if (!std::strcmp(kind, "poison") || !std::strcmp(kind, "swamp") ||
            !std::strcmp(kind, "sharp_rocks") || !std::strcmp(kind, "lever A"))
            bad(blocked + "patrol 1 0 loop route 2,0\n", 6, "walkable");
        else
            CHECK(load(blocked + "patrol 1 0 loop route 1,0 4,0\n"));
    }
    CHECK(load("@map\nS#...\n@enemies\npatrol 2 0 loop area 0 0 4 0 route 2,0 4,0\n"));
    CHECK(load("@legend\np = plate cyan\ni = ice\n> = belt right\n@map\nSpi>.\n"
               "@enemies\npatrol 1 0 loop route 1,0 2,0 3,0\n"));
    bad(map + "patrol 1 0 pingpong route 1,0 2,1\n", 5, "axis-aligned");
    bad(map + "patrol 1 0 loop route 1,0 2,0 2,1\n", 5, "axis-aligned");
    bad(map + "patrol 1 0 pingpong route 2,1\n", 5, "axis-aligned");
    bad("@map\nS.#..\n@enemies\npatrol 1 0 pingpong route 1,0 4,0\n",
        4, "crosses impassable");
    CHECK(load(map + "patrol 1 0 pingpong route 1,0 2,0 2,1\n"));
}

static void session()
{
    Adv::CatalogEntry entry{};
    std::strcpy(entry.folder, "josef");
    entry.mapCount = 2;
    std::strcpy(entry.maps[0], "01_lager.adv");
    std::strcpy(entry.maps[1], "02_vag.adv");
    Otr::Story::StoryState story;
    const Otr::Story::Script *script = nullptr;
    Adv::SdSession session;
    for (int policy = 0; policy < 4; ++policy) {
        CHECK(session.start("sdcard_bundle/otr/adventure", entry, loaded, world, story, script,
                            error, sizeof(error)));
        CHECK(!world.hasSuction());
        world.setSuction(true);
        const std::string rules = std::string("@rules\n") +
            (policy & 1 ? "carry: energy suit keys suction\n" : "") +
            (policy & 2 ? "reset: energy suit keys suction\n" : "") + "@map\nS.\n";
        CHECK(parse(rules));
        CHECK(session.travel(loaded, world, story, script, error, sizeof(error)));
        CHECK(world.hasSuction());
        std::vector<uint8_t> saved(world.saveSize() + Otr::Story::StoryState::SAVE_BYTES + 56);
        CHECK(session.save(world, story, saved.data(), saved.size()) == saved.size());
        CHECK(session.restart(loaded, world, story, script, error, sizeof(error)));
        CHECK(!world.hasSuction());
        CHECK(session.restore(saved.data(), saved.size(), loaded, world, story, script,
                              error, sizeof(error)));
        CHECK(world.hasSuction() && session.mapIndex() == 1);
    }
}

int main()
{
    equipment(); definitions(); limits(); blockedPatrol(); invalid(); session();
    std::printf("%s: %d passed, %d failed\n", failed ? "FAIL" : "OK", passed, failed);
    return failed ? 1 : 0;
}
