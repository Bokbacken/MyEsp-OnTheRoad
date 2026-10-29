// Host test for the FIRMWARE-SAFE .adv parser (main/adv_loader.*).
//
// It proves the on-device parser is in sync with the host reference parser (tools/adv_parse.*):
// every generated adventure parses to equivalent structures, loads into the REAL engine with
// World::load() and passes Script::validate(). It also checks the v0.17.0 features directly on the
// resources demo (@about, @treasures, @resources with caps/placements/glyph-clash, carry/reset),
// the lightweight header scan used by the SD menu catalog, and the error paths.
//
// Run from the repo root:
//   g++ -std=c++17 -Wall -Wextra -Imain -Itools -Icomponents/otr_engine/include
//   -Icomponents/otr_story/include tests/test_adv_loader.cpp main/adv_loader.cpp
//   tools/adv_parse.cpp components/otr_engine/otr_world.cpp components/otr_story/otr_story.cpp
//   -o tests/test_adv_loader && ./tests/test_adv_loader
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>

#include "adv_loader.h"
#include "adv_parse.h"
#include "otr_story.h"
#include "otr_world.h"

static int g_pass = 0, g_fail = 0;
#define CHECK(c)                                                                      \
    do {                                                                              \
        if (c) g_pass++;                                                              \
        else { g_fail++; std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); }  \
    } while (0)

static std::string sv(const char *s) { return s ? std::string(s) : std::string(); }

// Compares the firmware loader output against the host reference parser for one file.
static void crossCheck(const char *path)
{
    AdvDoc ref;
    std::string rerr;
    const bool refOk = parseAdvFile(path, ref, rerr);
    if (!refOk) { std::printf("FAIL ref parse %s: %s\n", path, rerr.c_str()); g_fail++; return; }

    auto L = std::make_unique<Adv::Loaded>();
    char err[128] = {0};
    if (!Adv::parseFile(path, *L, err, sizeof err)) {
        std::printf("FAIL fw parse %s: %s\n", path, err);
        g_fail++;
        return;
    }

    // Meta + about must match the reference exactly.
    CHECK(sv(L->id) == ref.id);
    CHECK(L->world == ref.world);
    CHECK(L->mapId == ref.mapId);
    CHECK(sv(L->titleSv) == ref.titleSv);
    CHECK(sv(L->titleEn) == ref.titleEn);
    CHECK(sv(L->descSv) == ref.descSv);
    CHECK(sv(L->descEn) == ref.descEn);

    // Map geometry + rows (after resource-glyph translation) must match.
    CHECK(L->rowCount == ref.map.h);
    bool rowsEqual = (L->rowCount == ref.map.h);
    for (int i = 0; rowsEqual && i < L->rowCount; i++)
        if (std::strcmp(L->rowPtrs[i], ref.map.rows[i]) != 0) rowsEqual = false;
    CHECK(rowsEqual);

    // Script table sizes must match.
    CHECK(L->script.npcCount == ref.script.npcCount);
    CHECK(L->script.nodeCount == ref.script.nodeCount);
    CHECK(L->script.flagCount == ref.script.flagCount);
    CHECK(L->script.counterCount == ref.script.counterCount);
    CHECK(L->script.itemCount == ref.script.itemCount);
    CHECK(L->script.questCount == ref.script.questCount);
    CHECK(L->treasureCount == ref.map.treasureCount);
    CHECK(L->darkCount == ref.map.darkCount);   // v0.18.0: @dark rects agree across both parsers
    CHECK((int)L->resourceCount == (int)ref.resources.size());
    CHECK((int)L->placementCount == (int)ref.resourcePlacements.size());

    // The firmware MapDef loads into the real engine and the script validates.
    Otr::World w;
    CHECK(w.load(L->map));
    char verr[2048] = {0};
    const int errs = L->script.validate(verr, sizeof(verr));
    if (errs) std::printf("validate %s:\n%s\n", path, verr);
    CHECK(errs == 0);
}

static void testGenerated()
{
    const char *files[] = {
        "docs/adventures/generated/testvarlden_01_testvarlden.adv",
        "docs/adventures/generated/minfaltet_01_minfaltet.adv",
        "docs/adventures/generated/byn_01_byn.adv",
        "docs/adventures/generated/josef_01_lager.adv",
        "docs/adventures/generated/josef_02_vag.adv",
        "docs/adventures/generated/01_darkdemo.adv",   // v0.18.0 dark/flashlight/pogo demo
        "docs/adventures/sokoban/sokoban.adv",
    };
    for (const char *f : files) crossCheck(f);
}

// Also cross-check the SD bundle copies (the files that actually ship on the card).
static void testSdBundle()
{
    const char *files[] = {
        "sdcard_bundle/otr/adventure/testvarlden/01_testvarlden.adv",
        "sdcard_bundle/otr/adventure/minfaltet/01_minfaltet.adv",
        "sdcard_bundle/otr/adventure/byn/01_byn.adv",
        "sdcard_bundle/otr/adventure/josef/01_lager.adv",
        "sdcard_bundle/otr/adventure/josef/02_vag.adv",
        "sdcard_bundle/otr/adventure/darkdemo/01_darkdemo.adv",   // v0.18.0 dark/flashlight/pogo demo
        "sdcard_bundle/otr/adventure/sokoban/sokoban.adv",
    };
    for (const char *f : files) crossCheck(f);
}

static void testResourcesDemo()
{
    const char *path = "docs/adventures/example_resources_demo.adv";
    auto L = std::make_unique<Adv::Loaded>();
    char err[128] = {0};
    if (!Adv::parseFile(path, *L, err, sizeof err)) {
        std::printf("FAIL parse demo: %s\n", err);
        g_fail++;
        return;
    }
    CHECK(sv(L->id) == "resources_demo");
    CHECK(L->world == 90 && L->mapId == 90);
    CHECK(sv(L->titleSv) == "Stenhuggarens stig");
    CHECK(sv(L->titleEn) == "The stonemason's path");
    CHECK(sv(L->cover) == "cover.png");

    // carry/reset masks.
    CHECK(L->carries(Adv::CARRY_ENERGY));
    CHECK(L->carries(Adv::CARRY_MAXENERGY));
    CHECK(L->carries(Adv::CARRY_ITEMS));
    CHECK(L->carries(Adv::CARRY_LIVES));
    CHECK(!L->carries(Adv::CARRY_SUIT));
    CHECK(L->resets(Adv::CARRY_SUIT));
    CHECK(!L->resets(Adv::CARRY_ENERGY));

    // One treasure (gem under the bush at 7,2).
    CHECK(L->treasureCount == 1);
    CHECK(L->treasures[0].x == 7 && L->treasures[0].y == 2);
    CHECK(L->treasures[0].item == Otr::Obj::GEM);

    // Two resources; stone cap 9, waterjug cap 3.
    CHECK(L->resourceCount == 2);
    const Adv::Resource *stone = nullptr, *jug = nullptr;
    for (int i = 0; i < L->resourceCount; i++) {
        if (L->resources[i].glyph == 's') stone = &L->resources[i];
        if (L->resources[i].glyph == 'w') jug = &L->resources[i];
    }
    CHECK(stone && sv(stone->id) == "stone" && stone->cap == 9 && sv(stone->image) == "stone.png");
    CHECK(jug && sv(jug->id) == "waterjug" && jug->cap == 3 && sv(jug->image) == "waterjug.png");
    CHECK(sv(stone ? stone->name.s[0] : nullptr) == "Sten");
    CHECK(sv(stone ? stone->name.s[1] : nullptr) == "Stone");

    // Resources become story items so give/take/has validate (stone + waterjug).
    CHECK(L->itemCount == 2);

    // Placements: three stones + two water jugs; the cells are translated to grass.
    CHECK(L->placementCount == 5);
    int sCnt = 0, wCnt = 0;
    for (int i = 0; i < L->placementCount; i++) {
        if (L->placements[i].glyph == 's') sCnt++;
        if (L->placements[i].glyph == 'w') wCnt++;
        // Cell must now be grass in the built rows.
        CHECK(L->rowPtrs[L->placements[i].y][L->placements[i].x] == '.');
    }
    CHECK(sCnt == 3 && wCnt == 2);

    // Loads + validates in the real engine (border is walkable, no wall needed).
    Otr::World w;
    CHECK(w.load(L->map));
    CHECK(w.width() == 16 && w.height() == 12);
    char verr[2048] = {0};
    CHECK(L->script.validate(verr, sizeof(verr)) == 0);
}

// True minesweeper SD campaign: every minkampanj level must parse, load into the engine with
// mineLevel()>0 (so the tap/flag/bomb UI and win-on-all-safe-revealed logic arm), and fog forced
// on. Also checks the mine_level activator, the minesweeper: alias and the out-of-range reject.
static void testMinesweeper()
{
    // All 12 generated campaign levels (both the docs copy and the SD bundle copy).
    for (int lvl = 1; lvl <= Otr::MINE_LEVELS; lvl++) {
        char docPath[128], sdPath[128];
        std::snprintf(docPath, sizeof docPath,
                      "docs/adventures/generated/minkampanj/%02d_niva%02d.adv", lvl, lvl);
        std::snprintf(sdPath, sizeof sdPath,
                      "sdcard_bundle/otr/adventure/minkampanj/%02d_niva%02d.adv", lvl, lvl);
        for (const char *path : {docPath, sdPath}) {
            crossCheck(path);
            auto L = std::make_unique<Adv::Loaded>();
            char err[128] = {0};
            if (!Adv::parseFile(path, *L, err, sizeof err)) {
                std::printf("FAIL parse %s: %s\n", path, err);
                g_fail++;
                continue;
            }
            Otr::World w;
            CHECK(w.load(L->map));
            CHECK(w.mineLevel() == lvl);  // mine_level: N plumbed through to the engine
        }
    }

    // mine_level activator: a hand-built map with mine_level sets mineLevel() and forces fog.
    {
        const char *m = "@rules\nmine_level: 5\n@map\nS...\n....\n....\n...X\n";
        auto L = std::make_unique<Adv::Loaded>();
        char err[128] = {0};
        CHECK(Adv::parse(m, std::strlen(m), *L, err, sizeof err));
        Otr::World w;
        CHECK(w.load(L->map));
        CHECK(w.mineLevel() == 5);
        CHECK(w.fog());  // mine_level implies fog
    }

    // minesweeper: true alias == mine_level 1.
    {
        const char *m = "@rules\nminesweeper: true\n@map\nS...\n....\n....\n...X\n";
        auto L = std::make_unique<Adv::Loaded>();
        char err[128] = {0};
        CHECK(Adv::parse(m, std::strlen(m), *L, err, sizeof err));
        Otr::World w;
        CHECK(w.load(L->map));
        CHECK(w.mineLevel() == 1);
        CHECK(w.fog());
    }

    // Out-of-range mine_level (>12) must be rejected by the parser.
    {
        const char *bad = "@rules\nmine_level: 13\n@map\nS...\n....\n....\n...X\n";
        auto L = std::make_unique<Adv::Loaded>();
        char err[128] = {0};
        CHECK(!Adv::parse(bad, std::strlen(bad), *L, err, sizeof err));
    }

    // mine_level: 0 is an ordinary (non-minesweeper) map, fog untouched.
    {
        const char *m = "@rules\nmine_level: 0\n@map\nS...\n....\n....\n...X\n";
        auto L = std::make_unique<Adv::Loaded>();
        char err[128] = {0};
        CHECK(Adv::parse(m, std::strlen(m), *L, err, sizeof err));
        Otr::World w;
        CHECK(w.load(L->map));
        CHECK(w.mineLevel() == 0);
    }
}

static void testHeaderScan()
{
    Adv::Header h;
    char err[128] = {0};
    CHECK(Adv::parseHeaderFile("sdcard_bundle/otr/adventure/josef/01_lager.adv", h, err, sizeof err));
    CHECK(sv(h.titleSv) == "Josef - Drömmaren");
    CHECK(sv(h.titleEn) == "Joseph - The Dreamer");
    CHECK(!sv(h.descSv).empty());
    CHECK(sv(h.author) == "MyEsp-OnTheRoad");
    CHECK(h.world == 4);
}

// Errors: unequal row widths, unknown section, glyph clash, too-wide map.
static void testErrors()
{
    auto L = std::make_unique<Adv::Loaded>();
    char err[128];

    const char *unequal = "@map\n...S..\n....\n";
    CHECK(!Adv::parse(unequal, std::strlen(unequal), *L, err, sizeof err));

    const char *unknown = "@bogus\n@map\nS.\n..\n";
    CHECK(!Adv::parse(unknown, std::strlen(unknown), *L, err, sizeof err));

    const char *clash = "@resources\nT : tree : X : Y : 1 :\n@map\nS.\n..\n";
    CHECK(!Adv::parse(clash, std::strlen(clash), *L, err, sizeof err));

    const char *noMap = "@meta\nid: x\n";
    CHECK(!Adv::parse(noMap, std::strlen(noMap), *L, err, sizeof err));

    // A valid minimal map with a walkable (wall-less) border must parse + load.
    const char *ok = "@map\nS...\n....\n....\n...X\n";
    CHECK(Adv::parse(ok, std::strlen(ok), *L, err, sizeof err));
    Otr::World w;
    CHECK(w.load(L->map));
    CHECK(w.width() == 4 && w.height() == 4);
}

// v0.18.0: @dark directive, f/j glyphs, and @treasures flashlight/pogo in the firmware loader.
static void testDarkFlashPogo()
{
    // Inline rect on the @dark header + extra rects as body lines; cap and clipping verified later.
    {
        const char *m =
            "@dark 1,1 2,2\n"
            "3,3 3,3\n"
            "@map\n"
            "S.....\n"
            "......\n"
            "......\n"
            "......\n"
            "......\n"
            ".....X\n";
        auto L = std::make_unique<Adv::Loaded>();
        char err[128] = {0};
        CHECK(Adv::parse(m, std::strlen(m), *L, err, sizeof err));
        CHECK(L->darkCount == 2);
        CHECK(L->darks[0].x0 == 1 && L->darks[0].y0 == 1 && L->darks[0].x1 == 2 && L->darks[0].y1 == 2);
        CHECK(L->darks[1].x0 == 3 && L->darks[1].y0 == 3 && L->darks[1].x1 == 3 && L->darks[1].y1 == 3);
        Otr::World w;
        CHECK(w.load(L->map));
        CHECK(w.darkCell(1, 1) && w.darkCell(2, 2));
        CHECK(!w.darkCell(0, 0));
        CHECK(w.darkCell(3, 3));
    }

    // Out-of-bounds rect is clipped to the map bounds at load time (not dropped if it overlaps).
    {
        const char *m = "@dark 2,2 9,9\n@map\nS...\n....\n....\n...X\n";
        auto L = std::make_unique<Adv::Loaded>();
        char err[128] = {0};
        CHECK(Adv::parse(m, std::strlen(m), *L, err, sizeof err));
        CHECK(L->darkCount == 1);
        Otr::World w;
        CHECK(w.load(L->map));
        CHECK(w.darkCell(3, 3));  // clipped corner still dark
        CHECK(w.darkCell(2, 2));
    }

    // Malformed @dark (wrong token count) is a hard parse error.
    {
        const char *bad = "@dark 1,1 2\n@map\nS...\n....\n....\n...X\n";
        auto L = std::make_unique<Adv::Loaded>();
        char err[128] = {0};
        CHECK(!Adv::parse(bad, std::strlen(bad), *L, err, sizeof err));
    }

    // Exceeding MAX_DARK is rejected instead of silently dropping map data.
    {
        std::string m = "@dark\n";
        for (int i = 0; i < Otr::MAX_DARK + 5; i++) m += "1,1 1,1\n";
        m += "@map\nS...\n....\n....\n...X\n";
        auto L = std::make_unique<Adv::Loaded>();
        char err[128] = {0};
        CHECK(!Adv::parse(m.c_str(), m.size(), *L, err, sizeof err));
        CHECK(strstr(err, "too many dark rectangles") != nullptr);
    }

    // f/j glyphs become flashlight / pogo pickups on the map (level 1).
    {
        const char *m = "@map\nS.f.\n.j..\n....\n...X\n";
        auto L = std::make_unique<Adv::Loaded>();
        char err[128] = {0};
        CHECK(Adv::parse(m, std::strlen(m), *L, err, sizeof err));
        Otr::World w;
        CHECK(w.load(L->map));
        CHECK((int)w.obj(2, 0) == (int)Otr::Obj::FLASH);
        CHECK((int)w.obj(1, 1) == (int)Otr::Obj::POGO);
    }

    // @treasures with flashlight/pogo ids: amount == level, round-tripped into the treasure list.
    {
        const char *m =
            "@treasures\n"
            "1 1 flashlight 2\n"
            "2 2 pogo 3\n"
            "@map\n"
            "S...\n"
            "....\n"
            "....\n"
            "...X\n";
        auto L = std::make_unique<Adv::Loaded>();
        char err[128] = {0};
        CHECK(Adv::parse(m, std::strlen(m), *L, err, sizeof err));
        CHECK(L->treasureCount == 2);
        CHECK(L->treasures[0].item == Otr::Obj::FLASH && L->treasures[0].amount == 2);
        CHECK(L->treasures[1].item == Otr::Obj::POGO && L->treasures[1].amount == 3);
        Otr::World w;
        CHECK(w.load(L->map));
    }
}

static void testGroupedSokoban()
{
    const char *map =
        "@legend\n"
        "a = plate 1\n"
        "A = gate 1\n"
        "@map\n"
        "S.RaA...Ro=.\n"
        "....Ra......\n"
        "............\n"
        "@plategroup 2: 9,0\n"
        "@gategroup 2: 10,0\n";
    auto L = std::make_unique<Adv::Loaded>();
    char err[128] = {0};
    CHECK(Adv::parse(map, std::strlen(map), *L, err, sizeof err));
    Otr::World w;
    CHECK(w.load(L->map));
    CHECK(!w.gateOpenForGroup(1) && !w.gateOpenForGroup(2));
    CHECK(!w.gateOpenAt(4, 0) && !w.gateOpenAt(10, 0));
    CHECK(!w.gateOpenForGroup(-1) && !w.gateOpenForGroup(Otr::PLATE_GROUPS));
    CHECK(!w.gateOpenAt(-1, 0) && !w.gateOpenAt(3, 0));
    CHECK(!w.playerCanEnter(4, 0) && !Otr::Planner::targetable(w, 10, 0));

    w.move(Otr::Dir::RIGHT);
    uint32_t ev = w.move(Otr::Dir::RIGHT);  // One of two group-1 plates is covered.
    CHECK(!(ev & Otr::EV_GATE_OPEN));
    CHECK(!w.gateOpenForGroup(1) && !w.gateOpenForGroup(2));
    w.move(Otr::Dir::DOWN);
    w.move(Otr::Dir::RIGHT);
    ev = w.move(Otr::Dir::RIGHT);           // The second group-1 plate is covered.
    CHECK(ev & Otr::EV_GATE_OPEN);
    CHECK(w.gateOpenForGroup(1) && !w.gateOpenForGroup(2));
    CHECK(w.gatesOpen() == w.gateOpenForGroup(0));
    CHECK(w.gateOpenAt(4, 0) && !w.gateOpenAt(10, 0));
    CHECK(w.playerCanEnter(4, 0) && Otr::Planner::targetable(w, 4, 0));
    CHECK(!Otr::Planner::targetable(w, 10, 0));

    w.move(Otr::Dir::DOWN);
    for (int i = 0; i < 3; i++) w.move(Otr::Dir::RIGHT);
    w.move(Otr::Dir::UP);
    w.move(Otr::Dir::UP);
    ev = w.move(Otr::Dir::RIGHT);  // Group 2 rock onto its coordinate-assigned plate.
    CHECK(ev & Otr::EV_GATE_OPEN);
    CHECK(w.gateOpenForGroup(1) && w.gateOpenForGroup(2));
    CHECK(w.gateOpenAt(4, 0) && w.gateOpenAt(10, 0));
    CHECK(Otr::Planner::targetable(w, 10, 0));

    for (int i = 0; i < 4; i++) w.move(Otr::Dir::LEFT);
    ev = w.move(Otr::Dir::LEFT);  // Push the group 1 rock off its plate.
    CHECK(ev & Otr::EV_GATE_CLOSE);
    CHECK(!w.gateOpenForGroup(1) && w.gateOpenForGroup(2));

    static uint8_t save[1024];
    const size_t n = w.save(save, sizeof(save));
    CHECK(n > 0);
    Otr::World restored;
    CHECK(restored.load(L->map));
    CHECK(restored.restore(save, n));
    CHECK(!restored.gateOpenForGroup(1) && restored.gateOpenForGroup(2));

    // Legend definitions override built-in map glyphs.
    const char *override =
        "@legend\n"
        "R = plate 1\n"
        "@map\n"
        "SR=\n";
    CHECK(Adv::parse(override, std::strlen(override), *L, err, sizeof err));
    CHECK(w.load(L->map));
    CHECK(w.tile(1, 0) == Otr::Tile::PLATE && w.rawObj(1, 0) == Otr::Obj::NONE);
    CHECK(w.gateOpenForGroup(1) == false);

    const char *reserved = "@legend\nS = plate 1\n@map\nS..\n";
    CHECK(!Adv::parse(reserved, std::strlen(reserved), *L, err, sizeof err));
    CHECK(std::strstr(err, "line 2") != nullptr);
    const char *duplicate = "@legend\na = plate 1\na = gate 1\n@map\nS..\n";
    CHECK(!Adv::parse(duplicate, std::strlen(duplicate), *L, err, sizeof err));
    CHECK(std::strstr(err, "line 3") != nullptr);

    const char *wrongCell =
        "@map\n"
        "S.o=\n"
        "@plategroup 1: 1,0\n";
    CHECK(!Adv::parse(wrongCell, std::strlen(wrongCell), *L, err, sizeof err));
    CHECK(std::strstr(err, "line 3") != nullptr);

    const char *hashWall =
        "@map\n"
        "####\n"
        "S..#\n"
        "...X\n";
    CHECK(Adv::parse(hashWall, std::strlen(hashWall), *L, err, sizeof err));
    CHECK(L->rowCount == 3 && std::strcmp(L->rowPtrs[0], "####") == 0);
    CHECK(w.load(L->map));
    CHECK(w.width() == 4 && w.height() == 3 && w.tile(0, 0) == Otr::Tile::WALL);
}

static void testSokobanDemo()
{
    auto L = std::make_unique<Adv::Loaded>();
    char err[128] = {0};
    CHECK(Adv::parseFile("docs/adventures/sokoban/sokoban.adv", *L, err, sizeof err));
    Otr::World w;
    CHECK(w.load(L->map));

    const int gateX[] = {10, 20, 30, 38};
    for (int group = 1; group <= 4; group++) {
        CHECK(!w.gateOpenForGroup(group));
        CHECK(!w.gateOpenAt(gateX[group - 1], 4));
    }

    for (int room = 0; room < 4; room++) {
        const int group = room + 1;
        const int roomStartX = room * 10;

        // Cover the first of this room's two plates; its gate must remain shut.
        const int firstPlateDx = room == 0 ? 1 : 2;
        for (int step = 0; step < firstPlateDx; step++) w.move(Otr::Dir::RIGHT);
        w.move(Otr::Dir::UP);
        CHECK(!w.gateOpenForGroup(group));
        CHECK(!w.gateOpenAt(gateX[room], 4));
        for (int other = group + 1; other <= 4; other++)
            CHECK(!w.gateOpenForGroup(other));

        // Walk around the second rock and push it onto the other plate.
        w.move(Otr::Dir::DOWN);
        for (int step = 0; step < 3; step++) w.move(Otr::Dir::RIGHT);
        const uint32_t ev = w.move(Otr::Dir::UP);
        CHECK(ev & Otr::EV_GATE_OPEN);
        CHECK(w.gateOpenForGroup(group));
        CHECK(w.gateOpenAt(gateX[room], 4));

        if (room < 3) {
            // The passage to the next chamber is reachable only after solving this room.
            w.move(Otr::Dir::DOWN);
            for (int step = 0; step < gateX[room] - (roomStartX + 6); step++)
                w.move(Otr::Dir::RIGHT);
            CHECK(w.playerCanEnter(gateX[room], 4));
            w.move(Otr::Dir::RIGHT);
            CHECK(!w.gateOpenForGroup(group + 1));
        } else {
            w.move(Otr::Dir::DOWN);
            for (int step = 0; step < 4; step++) w.move(Otr::Dir::RIGHT);
            CHECK(w.exitReached());
        }
    }
}

static void testS2Legend()
{
    auto L = std::make_unique<Adv::Loaded>();
    char err[128] = {};
    const char *map =
        "@legend\n"
        "v = LeVeR z\n"
        "= = gate Z\n"
        "i = ICE\n"
        "U = belt up\n"
        "> = belt right\n"
        "V = belt down\n"
        "< = belt left\n"
        "e = key red\n"
        "g = key green\n"
        "n = key blue\n"
        "y = key yellow\n"
        "q = key purple\n"
        "w = key CYAN\n"
        "E = door red\n"
        "G = door green\n"
        "N = door blue\n"
        "Y = door yellow\n"
        "J = door purple\n"
        "W = door cyan\n"
        "@rules\npar: 65535\n"
        "@map\nSvv=iU>V<egnyqwEGNYJWkKD\n";
    CHECK(Adv::parse(map, std::strlen(map), *L, err, sizeof err));
    CHECK(L->controlCount == 1 && L->map.controls == L->controls);
    CHECK(L->map.controlCount == 1 && L->controls[0].group == 35 && L->controls[0].steps == 0);
    Otr::World world;
    CHECK(world.load(L->map));
    CHECK(world.tile(1, 0) == Otr::Tile::LEVER && world.objAmount(1, 0) == 35);
    CHECK(world.tile(3, 0) == Otr::Tile::GATE && world.objAmount(3, 0) == 35);
    CHECK(world.tile(4, 0) == Otr::Tile::ICE);
    for (int d = 0; d < 4; d++) {
        CHECK(world.tile(5 + d, 0) == (Otr::Tile)((int)Otr::Tile::BELT_UP + d));
        CHECK(!Otr::Planner::targetable(world, 5 + d, 0));
    }
    CHECK(!Otr::Planner::targetable(world, 4, 0));
    for (int c = 0; c < 6; c++) {
        CHECK(world.rawObj(9 + c, 0) == (Otr::Obj)((int)Otr::Obj::KEY_RED + c));
        CHECK(world.rawObj(15 + c, 0) == (Otr::Obj)((int)Otr::Obj::DOOR_RED + c));
    }
    CHECK(world.rawObj(21, 0) == Otr::Obj::KEY && world.rawObj(22, 0) == Otr::Obj::KEY);
    CHECK(world.rawObj(23, 0) == Otr::Obj::DOOR && world.par() == 65535);
    world.move(Otr::Dir::RIGHT);  // Face a lever in the shared toggle group.
    CHECK(world.activate() & Otr::EV_GATE_OPEN);
    CHECK(world.leverOnAt(1, 0) && world.leverOnAt(2, 0));
    CHECK(world.gateOpenForGroup(35));
    CHECK(world.activate() & Otr::EV_GATE_CLOSE);
    CHECK(!world.leverOnAt(1, 0) && !world.leverOnAt(2, 0));

    // Resources keep the S1 priority: a legend definition wins even if declared later.
    for (const char *text : {
             "@resources\ns : stone : Stone\n@legend\ns = ice\n@map\nSs\n",
             "@legend\ns = ice\n@resources\ns : stone : Stone\n@map\nSs\n"}) {
        CHECK(Adv::parse(text, std::strlen(text), *L, err, sizeof err));
        CHECK(L->resourceCount == 1 && L->placementCount == 0);
        CHECK(world.load(L->map) && world.tile(1, 0) == Otr::Tile::ICE);
        CHECK(L->controlCount == 0 && L->map.controls == nullptr && L->map.par == 0);
    }

    // Every group fits the fixed backing, including group 0 and lowercase ids.
    std::string text = "@legend\n", row = "S";
    for (int g = 0; g < Otr::PLATE_GROUPS; g++) {
        const char glyph = g < 26 ? (char)('a' + g) : (char)('A' + g - 26);
        const char group = g < 10 ? (char)('0' + g) : (char)('a' + g - 10);
        text += std::string(1, glyph) + " = lever " + group + "\n";
        row += glyph;
    }
    text += "@map\n" + row + "\n";
    CHECK(Adv::parse(text.c_str(), text.size(), *L, err, sizeof err));
    CHECK(L->controlCount == Otr::PLATE_GROUPS);
    for (int g = 0; g < Otr::PLATE_GROUPS; g++)
        CHECK(L->controls[g].group == g && L->controls[g].steps == 0);
    CHECK(world.load(L->map));
}

static void testS2Effects()
{
    auto L = std::make_unique<Adv::Loaded>();
    char err[128] = {};
    Otr::World world;
    const char *lever =
        "@timedgate 1: 2 # subsequent successful displacements\n"
        "@legend\nv = lever 1\nG = gate 1\n"
        "@map\nS..G\nv...\n";
    CHECK(Adv::parse(lever, std::strlen(lever), *L, err, sizeof err));
    CHECK(L->controls[0].steps == 2 && world.load(L->map));
    world.activate();
    CHECK(world.timerRemaining(1) == 2 && world.gateOpenAt(3, 0));
    world.move(Otr::Dir::UP);
    CHECK(world.timerRemaining(1) == 2 && world.steps() == 0);
    world.move(Otr::Dir::RIGHT);
    CHECK(world.timerRemaining(1) == 1);
    world.move(Otr::Dir::LEFT);
    CHECK(world.timerRemaining(1) == 0 && !world.gateOpenAt(3, 0));

    const char *plate =
        "@map\nSo=.\n@plategroup A: 1,0\n@gategroup A: 2,0\n@timedgate a: 2\n";
    CHECK(Adv::parse(plate, std::strlen(plate), *L, err, sizeof err));
    CHECK(L->controls[0].group == 10 && world.load(L->map));
    world.move(Otr::Dir::RIGHT);
    CHECK(world.timerRemaining(10) == 2 && world.gateOpenAt(2, 0));
    world.move(Otr::Dir::LEFT);
    CHECK(world.timerRemaining(10) == 1);
    world.move(Otr::Dir::RIGHT);  // Expiry ticks before the new entry rearms the timer.
    CHECK(world.timerRemaining(10) == 2 && world.gateOpenAt(2, 0));

    const char *rock =
        "@legend\ns = plate 1\nG = gate 1\n@timedgate 1: 3\n@map\nSRsG.\n";
    CHECK(Adv::parse(rock, std::strlen(rock), *L, err, sizeof err));
    CHECK(world.load(L->map));
    world.move(Otr::Dir::RIGHT);
    CHECK(world.timerRemaining(1) == 3 && world.gateOpenAt(3, 0));

    const char *motion =
        "@legend\ni = ice\n> = belt right\n@rules\npar: 4\n@map\nSii>..X\n";
    CHECK(Adv::parse(motion, std::strlen(motion), *L, err, sizeof err));
    CHECK(world.load(L->map) && world.par() == 4 && world.parMet());
    world.move(Otr::Dir::RIGHT);
    CHECK(world.playerX() == 4 && world.steps() == 4 && world.parMet());
    world.move(Otr::Dir::RIGHT);
    CHECK(world.steps() == 5 && !world.parMet());

    const char *colors =
        "@legend\ns = key red\nw = door blue\nt = door red\n"
        "@map\nSsw.t\n.....\n";
    CHECK(Adv::parse(colors, std::strlen(colors), *L, err, sizeof err));
    CHECK(world.load(L->map));
    world.move(Otr::Dir::RIGHT);
    CHECK(world.keys(Otr::KeyColor::RED) == 1 && world.keys() == 0);
    CHECK(world.move(Otr::Dir::RIGHT) & Otr::EV_NEED_KEY);
    CHECK(world.keys(Otr::KeyColor::RED) == 1);
    world.move(Otr::Dir::DOWN);
    for (int i = 0; i < 3; i++) world.move(Otr::Dir::RIGHT);
    CHECK(world.move(Otr::Dir::UP) & Otr::EV_DOOR);
    CHECK(world.keys(Otr::KeyColor::RED) == 0 && world.rawObj(4, 0) == Otr::Obj::NONE);
}

static void testS2Errors()
{
    auto L = std::make_unique<Adv::Loaded>();
    char err[128] = {};
    struct Bad { const char *text; int line; const char *message; };
    const Bad cases[] = {
        {"@legend\nv = lever\n@map\nSv\n", 2, "group"},
        {"@legend\nv = lever 36\n@map\nSv\n", 2, "group"},
        {"@legend\nv = lever ?\n@map\nSv\n", 2, "group"},
        {"@legend\ni = ice extra\n@map\nSi\n", 2, "argument"},
        {"@legend\nv = belt\n@map\nSv\n", 2, "direction"},
        {"@legend\nv = belt north\n@map\nSv\n", 2, "direction"},
        {"@legend\nv = belt up extra\n@map\nSv\n", 2, "legend"},
        {"@legend\nv = key\n@map\nSv\n", 2, "color"},
        {"@legend\nv = door orange\n@map\nSv\n", 2, "color"},
        {"@legend\nv = key red extra\n@map\nSv\n", 2, "legend"},
        {"@legend\nv = teleport\n@map\nSv\n", 2, "kind"},
        {"@legend\nS = ice\n@map\nS\n", 2, "reserved"},
        {"@legend\n@ = ice\n@map\nS\n", 2, "section"},
        {"@legend\n = ice\n@map\nS\n", 2, "legend"},
        {"@legend\nv = ice\nv = lever 1\n@map\nSv\n", 3, "duplicate"},
        {"@resources\nk : stone\n@map\nSk\n", 2, "built-in"},
        {"@resources\ns : stone\ns : sand\n@map\nSs\n", 3, "duplicate"},
        {"@timedgate 1\n@map\nSo=\n", 1, "timedgate"},
        {"@timedgate 36: 2\n@map\nSo=\n", 1, "timedgate"},
        {"@timedgate 1: 0\n@map\nSo=\n", 1, "timedgate"},
        {"@timedgate 1: -1\n@map\nSo=\n", 1, "timedgate"},
        {"@timedgate 1: 65536\n@map\nSo=\n", 1, "timedgate"},
        {"@timedgate 1: 999999999999999999999\n@map\nSo=\n", 1, "timedgate"},
        {"@timedgate 1: 2 extra\n@map\nSo=\n", 1, "timedgate"},
        {"@timedgate a: 2\n@timedgate A: 3\n@map\nSo=\n", 2, "duplicate"},
        {"@rules\npar: 0\n@map\nS\n", 2, "par"},
        {"@rules\npar 2\n@map\nS\n", 2, "par"},
        {"@rules\npar:\n@map\nS\n", 2, "par"},
        {"@rules\npar: +2\n@map\nS\n", 2, "par"},
        {"@rules\npar: -1\n@map\nS\n", 2, "par"},
        {"@rules\npar: 65536\n@map\nS\n", 2, "par"},
        {"@rules\npar: 1x\n@map\nS\n", 2, "par"},
        {"@rules\npar: 2 extra\n@map\nS\n", 2, "par"},
        {"@legend\nv = lever 0\n@map\nSvo=\n", 4, "mix"},
        {"@legend\nv = lever 1\n@map\nSvo=\n@plategroup 1: 2,0\n", 4, "mix"},
        {"@timedgate 0: 2\n@map\nSo.\n", 1, "exactly one"},
        {"@timedgate 0: 2\n@map\nS.=\n", 1, "exactly one"},
        {"@timedgate 0: 2\n@map\nSoo=\n", 1, "exactly one"},
        {"@legend\nv = lever 1\nG = gate 1\n@map\nSvvG\n@timedgate 1: 2\n", 6, "exactly one"},
        {"@legend\ni = ice\n@map\nSi\n@plategroup 1: 1,0\n", 5, "not a plate"},
        {"@treasures\n1 0 gem 255\n@map\nSo=\n", 2, "treasure cannot share"},
        {"@map\nSo=\n@treasures\n2 0 key 255\n", 4, "treasure cannot share"},
        {"@treasures\n1 0 gem 255\n@legend\nv = lever 1\n@map\nSv\n", 2, "treasure cannot share"},
        {"@legend\ns = plate Z\n@map\nSs\n@treasures\n1 0 gem 255\n", 6, "treasure cannot share"},
        {"@legend\nG = gate Z\n@map\nSG\n@treasures\n1 0 potion 255\n", 6, "treasure cannot share"},
        {"@map\nSo=\n@plategroup 1: 1,0\n@timedgate 1: 2\n@gategroup 1: 2,0\n"
         "@treasures\n1 0 gem 255\n", 7, "treasure cannot share"},
    };
    for (const Bad &bad : cases) {
        CHECK(!Adv::parse(bad.text, std::strlen(bad.text), *L, err, sizeof err));
        char prefix[32];
        std::snprintf(prefix, sizeof prefix, "line %d:", bad.line);
        CHECK(std::strncmp(err, prefix, std::strlen(prefix)) == 0);
        CHECK(std::strstr(err, bad.message) != nullptr);
    }
    const char *maxTimer = "@timedgate 0: 65535\n@map\nSo=\n";
    CHECK(Adv::parse(maxTimer, std::strlen(maxTimer), *L, err, sizeof err));
    CHECK(L->controlCount == 1 && L->controls[0].steps == 65535);
    const char *ordinaryTreasure =
        "@legend\no = ice\n@treasures\n1 0 gem 1\n@map\nSo\n";
    CHECK(Adv::parse(ordinaryTreasure, std::strlen(ordinaryTreasure), *L, err, sizeof err));
    Otr::World world;
    CHECK(world.load(L->map));
    CHECK(world.tile(1, 0) == Otr::Tile::ICE && world.rawObj(1, 0) == Otr::Obj::GEM);
}

static void testZeroTreasureAmounts()
{
    const char *text =
        "@rules\n"
        "start_energy: 4\n"
        "energy_cap: 10\n"
        "potion_default: 3\n"
        "heart_default: 2\n"
        "@map\n"
        "S...\n"
        "@treasures\n"
        "1 0 potion 0\n"
        "2 0 heart 0\n";
    auto L = std::make_unique<Adv::Loaded>();
    char err[128] = {0};
    CHECK(Adv::parse(text, std::strlen(text), *L, err, sizeof err));
    CHECK(L->treasureCount == 2 && L->treasures[0].amount == 0 && L->treasures[1].amount == 0);
    Otr::World world;
    CHECK(world.load(L->map));
    world.setEnergy(1);
    CHECK(world.move(Otr::Dir::RIGHT) & Otr::EV_POTION);
    CHECK(world.energy() == 4);
    CHECK(world.move(Otr::Dir::RIGHT) & Otr::EV_HEART);
    CHECK(world.maxEnergy() == 6 && world.energy() == 6);

    const char *outOfRange = "@map\nS..\n@treasures\n1 0 potion 256\n";
    CHECK(!Adv::parse(outOfRange, std::strlen(outOfRange), *L, err, sizeof err));
}

int main()
{
    testGenerated();
    testSdBundle();
    testResourcesDemo();
    testMinesweeper();
    testHeaderScan();
    testErrors();
    testDarkFlashPogo();
    testGroupedSokoban();
    testSokobanDemo();
    testS2Legend();
    testS2Effects();
    testS2Errors();
    testZeroTreasureAmounts();
    std::printf("\n%s: %d passed, %d failed\n", g_fail ? "FAIL" : "OK", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
