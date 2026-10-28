// test_adv_catalog.cpp - host tests for the SD adventure catalog scanner (adv_catalog) and the
// SD-backed session (adv_sd_session): folder enumeration + ordering, header extraction, map paths,
// loading maps into the engine, carry/reset of player resources between maps, and the per-folder
// save/restore roundtrip. Uses sdcard_bundle/otr/adventure as the fixture.
//
// Run from the repo root:
//   g++ -std=c++17 -Wall -Wextra -Imain -Itools -Icomponents/otr_engine/include
//   -Icomponents/otr_story/include tests/test_adv_catalog.cpp main/adv_catalog.cpp
//   main/adv_sd_session.cpp main/adv_loader.cpp tools/adv_parse.cpp
//   components/otr_engine/otr_world.cpp components/otr_story/otr_story.cpp
//   -o tests/test_adv_catalog && ./tests/test_adv_catalog
#include <cstdio>
#include <cstring>
#include <memory>

#include "adv_catalog.h"
#include "adv_sd_session.h"
#include "otr_story.h"
#include "otr_world.h"

static int g_pass = 0, g_fail = 0;
#define CHECK(cond, msg)                                                      \
    do {                                                                      \
        if (cond) {                                                          \
            g_pass++;                                                        \
        } else {                                                            \
            g_fail++;                                                        \
            printf("FAIL: %s  (%s:%d)\n", msg, __FILE__, __LINE__);         \
        }                                                                   \
    } while (0)

static const char *ROOT = "sdcard_bundle/otr/adventure";

static int folderIndex(const Adv::Catalog &c, const char *name)
{
    for (int i = 0; i < c.count; i++)
        if (strcmp(c.items[i].folder, name) == 0) return i;
    return -1;
}

static void testCatalog(const Adv::Catalog &c)
{
    // The bundle also ships resources, minesweeper, dark/pogo and four-room Sokoban demos.
    CHECK(c.count == 8, "catalog found 8 adventures");
    // Alphabetical order: byn, darkdemo, josef, minfaltet, minkampanj, resdemo, sokoban, testvarlden.
    CHECK(folderIndex(c, "byn") == 0, "byn sorts first");
    CHECK(folderIndex(c, "darkdemo") == 1, "darkdemo sorts second");
    CHECK(folderIndex(c, "josef") == 2, "josef sorts third");
    CHECK(folderIndex(c, "minfaltet") == 3, "minfaltet sorts fourth");
    CHECK(folderIndex(c, "minkampanj") == 4, "minkampanj sorts fifth");
    CHECK(folderIndex(c, "resdemo") == 5, "resdemo sorts sixth");
    int si = folderIndex(c, "sokoban");
    CHECK(si == 6, "sokoban sorts seventh");
    CHECK(si >= 0 && c.items[si].mapCount == 1, "sokoban has one four-room map");
    CHECK(si >= 0 && strcmp(c.items[si].maps[0], "sokoban.adv") == 0,
          "sokoban map is the combined demo");
    CHECK(folderIndex(c, "testvarlden") == 7, "testvarlden sorts eighth");

    // minkampanj is the true minesweeper campaign: 12 ordered maps.
    int mi = folderIndex(c, "minkampanj");
    CHECK(mi >= 0 && c.items[mi].mapCount == 12, "minkampanj has 12 maps");

    int ji = folderIndex(c, "josef");
    CHECK(ji >= 0, "josef in catalog");
    if (ji >= 0) {
        const Adv::CatalogEntry &e = c.items[ji];
        CHECK(e.mapCount == 2, "josef has 2 maps");
        CHECK(strcmp(e.maps[0], "01_lager.adv") == 0, "josef map 0 = 01_lager.adv");
        CHECK(strcmp(e.maps[1], "02_vag.adv") == 0, "josef map 1 = 02_vag.adv");
        CHECK(e.headerOk, "josef header parsed");
        CHECK(strstr(e.header.titleSv, "Josef") != nullptr, "josef title.sv has 'Josef'");
        CHECK(strstr(e.header.titleEn, "Joseph") != nullptr, "josef title.en has 'Joseph'");
        CHECK(strcmp(e.header.author, "MyEsp-OnTheRoad") == 0, "josef author");
        char p[256];
        CHECK(Adv::mapPath(p, sizeof(p), ROOT, e, 0) &&
                  strcmp(p, "sdcard_bundle/otr/adventure/josef/01_lager.adv") == 0,
              "mapPath for josef/0");
        CHECK(!Adv::mapPath(p, sizeof(p), ROOT, e, 5), "mapPath out of range fails");
    }

    int bi = folderIndex(c, "byn");
    CHECK(bi >= 0 && c.items[bi].mapCount == 1, "byn has 1 map");
    CHECK(bi >= 0 && strstr(c.items[bi].header.titleSv, "Byn") != nullptr, "byn title.sv 'Byn'");
}

static void testMissingFolder()
{
    Adv::Catalog c;
    int n = Adv::scanCatalog("sdcard_bundle/otr/adventure/does_not_exist", c);
    CHECK(n == 0 && c.count == 0, "missing folder -> empty catalog");
}

static void testSession(const Adv::Catalog &c)
{
    int ji = folderIndex(c, "josef");
    if (ji < 0) return;
    auto scratch = std::make_unique<Adv::Loaded>();
    Otr::World w;
    Otr::Story::StoryState story;
    const Otr::Story::Script *script = nullptr;
    char err[96];

    Adv::SdSession s;
    bool ok = s.start(ROOT, c.items[ji], *scratch, w, story, script, err, sizeof(err));
    CHECK(ok, "session start josef");
    if (!ok) {
        printf("  start err: %s\n", err);
        return;
    }
    CHECK(s.mapCount() == 2 && s.mapIndex() == 0, "session on map 0 of 2");
    CHECK(s.hasNext(), "josef has a next map");
    CHECK(strcmp(s.folder(), "josef") == 0, "session folder josef");
    CHECK(w.width() > 0 && w.height() > 0, "world loaded (map 0)");

    // Set a distinct player state, then travel. josef map 0 carries energy/maxenergy/lives, resets
    // suit (carry list has no suit).
    w.setMaxEnergy(16);
    w.setEnergy(11);
    w.setSuitCharges(5);
    w.setKeys(3);
    const int oldEnergy = w.energy(), oldMax = w.maxEnergy();

    ok = s.travel(*scratch, w, story, script, err, sizeof(err));
    CHECK(ok, "travel to map 1");
    if (!ok) {
        printf("  travel err: %s\n", err);
        return;
    }
    CHECK(s.mapIndex() == 1, "now on map 1");
    CHECK(w.energy() == oldEnergy, "energy carried across maps");
    CHECK(w.maxEnergy() == oldMax, "max energy carried across maps");
    CHECK(w.suitCharges() == 0, "suit reset (not in carry list)");
    CHECK(w.keys() == 0, "keys not carried (not in josef carry list)");
    CHECK(!s.hasNext(), "no map after map 1");
}

static void testSaveRoundtrip(const Adv::Catalog &c)
{
    int ji = folderIndex(c, "josef");
    if (ji < 0) return;
    auto scratch = std::make_unique<Adv::Loaded>();
    Otr::World w;
    Otr::Story::StoryState story;
    const Otr::Story::Script *script = nullptr;
    char err[96];

    Adv::SdSession s;
    if (!s.start(ROOT, c.items[ji], *scratch, w, story, script, err, sizeof(err))) return;
    // Advance to map 1 and tweak state so the save is non-trivial.
    s.travel(*scratch, w, story, script, err, sizeof(err));
    w.setEnergy(7);
    w.move(Otr::Dir::DOWN);   // change some mutable world state
    const int savedMap = s.mapIndex(), savedEnergy = w.energy();

    auto buf = std::make_unique<uint8_t[]>(Adv::MAX_FILE);
    size_t n = s.save(w, story, buf.get(), Adv::MAX_FILE);
    CHECK(n > 0, "session save produced bytes");

    // Restore into a brand new session/world (as the UI does on "continue").
    auto scratch2 = std::make_unique<Adv::Loaded>();
    Otr::World w2;
    Otr::Story::StoryState story2;
    const Otr::Story::Script *script2 = nullptr;
    Adv::SdSession s2;
    // start() first (UI always starts the adventure, then restores a save over it).
    bool st = s2.start(ROOT, c.items[ji], *scratch2, w2, story2, script2, err, sizeof(err));
    CHECK(st, "second session start");
    bool rok = s2.restore(buf.get(), n, *scratch2, w2, story2, script2, err, sizeof(err));
    CHECK(rok, "session restore");
    if (!rok) {
        printf("  restore err: %s\n", err);
        return;
    }
    CHECK(s2.mapIndex() == savedMap, "restored map index matches");
    CHECK(w2.energy() == savedEnergy, "restored energy matches");

    // A corrupt image must be rejected and leave the (started) state intact.
    buf[4] = 0xFF;   // smash the version
    Adv::SdSession s3;
    s3.start(ROOT, c.items[ji], *scratch2, w2, story2, script2, err, sizeof(err));
    bool bad = s3.restore(buf.get(), n, *scratch2, w2, story2, script2, err, sizeof(err));
    CHECK(!bad, "corrupt save rejected");

    // A save for a different folder must be refused.
    size_t n2 = s.save(w, story, buf.get(), Adv::MAX_FILE);
    (void)n2;
    int bi = folderIndex(c, "byn");
    if (bi >= 0) {
        Adv::SdSession s4;
        s4.start(ROOT, c.items[bi], *scratch2, w2, story2, script2, err, sizeof(err));
        // Re-save josef image (buf still corrupt above, so rebuild it clean):
        Adv::SdSession sj;
        sj.start(ROOT, c.items[ji], *scratch, w, story, script, err, sizeof(err));
        size_t nj = sj.save(w, story, buf.get(), Adv::MAX_FILE);
        bool mism = s4.restore(buf.get(), nj, *scratch2, w2, story2, script2, err, sizeof(err));
        CHECK(!mism, "save from another folder refused");
    }
}

// Every adventure in the bundle must parse, load into the engine and be walkable through all of its
// maps (start -> travel to the end), with the engine accepting each map and a save surviving a
// restore. This is the regression that guards the generated/example .adv files shipped on the card.
static void testLoadEveryAdventure(const Adv::Catalog &c)
{
    CHECK(c.count > 0, "bundle has at least one adventure to load");
    auto scratch = std::make_unique<Adv::Loaded>();
    auto buf = std::unique_ptr<uint8_t[]>(new uint8_t[Adv::MAX_FILE]);
    for (int i = 0; i < c.count; i++) {
        const Adv::CatalogEntry &e = c.items[i];
        Otr::World w;
        Otr::Story::StoryState story;
        const Otr::Story::Script *script = nullptr;
        Adv::SdSession s;
        char err[96];
        bool ok = s.start(ROOT, e, *scratch, w, story, script, err, sizeof(err));
        if (!ok) printf("  '%s' start err: %s\n", e.folder, err);
        CHECK(ok, "every adventure: first map loads into the engine");
        if (!ok) continue;
        CHECK(w.width() > 0 && w.height() > 0, "every adventure: world has a map");
        CHECK(s.mapCount() == e.mapCount, "every adventure: session map count == catalog");

        // Walk through the remaining maps; each must load cleanly (carry/reset applied internally).
        int visited = 1;
        while (s.hasNext()) {
            bool t = s.travel(*scratch, w, story, script, err, sizeof(err));
            if (!t) printf("  '%s' travel err: %s\n", e.folder, err);
            CHECK(t, "every adventure: travel to next map");
            if (!t) break;
            CHECK(w.width() > 0 && w.height() > 0, "every adventure: next map has a map");
            visited++;
        }
        CHECK(visited == e.mapCount, "every adventure: visited all maps");

        // A save made on the last map must restore cleanly over a fresh start.
        size_t n = s.save(w, story, buf.get(), Adv::MAX_FILE);
        CHECK(n > 0, "every adventure: save produced bytes");
        if (n > 0) {
            Otr::World w2;
            Otr::Story::StoryState story2;
            const Otr::Story::Script *script2 = nullptr;
            Adv::SdSession s2;
            s2.start(ROOT, e, *scratch, w2, story2, script2, err, sizeof(err));
            bool rok = s2.restore(buf.get(), n, *scratch, w2, story2, script2, err, sizeof(err));
            if (!rok) printf("  '%s' restore err: %s\n", e.folder, err);
            CHECK(rok, "every adventure: save restores");
        }
    }
}

// True minesweeper SD campaign: starting the minkampanj session arms the engine's minesweeper mode
// (mineLevel()>0) on every map, and the SD travel path (the same one an X-exit / minesweeper WIN
// drives) advances through all 12 ordered levels, each with its own mine_level.
static void testMinesweeperSession(const Adv::Catalog &c)
{
    int mi = folderIndex(c, "minkampanj");
    CHECK(mi >= 0, "minkampanj in catalog");
    if (mi < 0) return;

    auto scratch = std::make_unique<Adv::Loaded>();
    Otr::World w;
    Otr::Story::StoryState story;
    const Otr::Story::Script *script = nullptr;
    char err[96];

    Adv::SdSession s;
    bool ok = s.start(ROOT, c.items[mi], *scratch, w, story, script, err, sizeof(err));
    CHECK(ok, "minkampanj session start");
    if (!ok) {
        printf("  start err: %s\n", err);
        return;
    }
    CHECK(s.mapCount() == 12, "minkampanj session has 12 maps");

    // Level 1 on map 0, and the minesweeper engine mode is armed from mine_level.
    CHECK(w.mineLevel() == 1, "map 0 arms minesweeper level 1");
    CHECK(w.fog(), "minesweeper map has fog forced on");

    // Travel through the whole campaign (the SD WIN -> advance path), checking the level climbs.
    int expected = 2;
    while (s.hasNext()) {
        bool t = s.travel(*scratch, w, story, script, err, sizeof(err));
        CHECK(t, "minkampanj travel to next level");
        if (!t) {
            printf("  travel err: %s\n", err);
            break;
        }
        CHECK(w.mineLevel() == expected, "next map arms its own minesweeper level");
        CHECK(w.fog(), "every minesweeper map has fog forced on");
        expected++;
    }
    CHECK(expected == 13, "advanced through all 12 minesweeper levels");
    CHECK(!s.hasNext(), "no map after level 12");
}

int main()
{
    auto cat = std::make_unique<Adv::Catalog>();
    int n = Adv::scanCatalog(ROOT, *cat);
    printf("scanCatalog -> %d adventures\n", n);
    testCatalog(*cat);
    testMissingFolder();
    testSession(*cat);
    testSaveRoundtrip(*cat);
    testLoadEveryAdventure(*cat);
    testMinesweeperSession(*cat);

    printf("\n%s: %d passed, %d failed\n", g_fail ? "FAILURES" : "OK", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
