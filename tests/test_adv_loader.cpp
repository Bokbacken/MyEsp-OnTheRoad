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

int main()
{
    testGenerated();
    testSdBundle();
    testResourcesDemo();
    testHeaderScan();
    testErrors();
    std::printf("\n%s: %d passed, %d failed\n", g_fail ? "FAIL" : "OK", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
