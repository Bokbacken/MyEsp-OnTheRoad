// Host test for the .adv importer (tools/adv_parse.*) and the generated adventures.
//
// It proves the ASCII authoring format round-trips into the REAL engine:
//   * the Hebron example (docs/adventures/example_joseph_hebron.adv) parses, loads
//     with World::load() and passes Script::validate();
//   * every GENERATED adventure (docs/adventures/generated/*.adv, produced by
//     tools/adv_export.cpp from the compiled data) parses, loads and validates;
//   * v0.17.0 features: @about, @treasures (hidden item revealed when the covering
//     object is removed), @resources (glyph -> item + cap, placements, glyph-clash
//     detection), carry/reset, an 80x80 map and a walkable border (movement blocked
//     at the edge even without a wall).
//
// Run from the repo root:
//   g++ -std=c++17 -Wall -Wextra -Itools -Icomponents/otr_engine/include -Icomponents/otr_story/include
//       tests/test_adv_parser.cpp tools/adv_parse.cpp components/otr_engine/otr_world.cpp
//       components/otr_story/otr_story.cpp -o tests/test_adv_parser && ./tests/test_adv_parser
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include "adv_parse.h"
#include "otr_story.h"
#include "otr_world.h"

static int g_pass = 0, g_fail = 0;
#define CHECK(c)                                                             \
    do {                                                                     \
        if (c) g_pass++;                                                     \
        else { g_fail++; std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } \
    } while (0)

// Finds a resource by glyph in a parsed doc (nullptr if absent).
static const AdvResource *resByGlyph(const AdvDoc &d, char g)
{
    for (const AdvResource &r : d.resources)
        if (r.glyph == g) return &r;
    return nullptr;
}

static void writeTmp(const std::string &path, const std::string &body)
{
    std::ofstream o(path);
    o << body;
}

// --- 1. The hand-written Hebron example (backwards compatible) -------------------------------
static void testHebronExample(const std::string &path)
{
    AdvDoc doc;
    std::string err;
    if (!parseAdvFile(path, doc, err)) {
        std::printf("parse error: %s\n", err.c_str());
        g_fail++;
        return;
    }
    CHECK(doc.id == "joseph_camp");
    CHECK(doc.world == 4);
    CHECK(doc.mapId == 4);
    // @meta name.*/desc.* now feed the @about fields (A1 legacy fallback).
    CHECK(doc.titleSv == "Jakobs läger");
    CHECK(doc.titleEn == "Jacob's camp");
    CHECK(!doc.descSv.empty());

    CHECK(doc.script.npcCount == 3);
    CHECK(doc.script.flagCount == 4);
    CHECK(doc.script.counterCount == 1);
    CHECK(doc.script.itemCount == 1);
    CHECK(doc.script.questCount == 2);
    CHECK(doc.script.nodeCount >= 10);

    Otr::World w;
    CHECK(w.load(doc.map));
    CHECK(w.width() == 40);
    CHECK(w.height() == 30);
    CHECK(w.mapId() == 4);

    int x = -1, y = -1;
    CHECK(w.findNpc(1, x, y));
    CHECK(w.findNpc(2, x, y));
    CHECK(w.findNpc(3, x, y));
    CHECK(!w.findNpc(4, x, y));

    char verr[1024] = {0};
    const int errs = doc.script.validate(verr, sizeof(verr));
    if (errs != 0) std::printf("validate errors:\n%s\n", verr);
    CHECK(errs == 0);

    const Otr::Story::Npc *n3 = doc.script.npcByNum(3);
    CHECK(n3 != nullptr && std::strcmp(n3->id, "ruben") == 0);
}

// --- 2. Every generated adventure parses, loads and validates (C10 / D13) --------------------
static void testGeneratedAdventures()
{
    const char *files[] = {
        "docs/adventures/generated/testvarlden_01_testvarlden.adv",
        "docs/adventures/generated/minfaltet_01_minfaltet.adv",
        "docs/adventures/generated/byn_01_byn.adv",
        "docs/adventures/generated/josef_01_lager.adv",
        "docs/adventures/generated/josef_02_vag.adv",
    };
    for (const char *f : files) {
        AdvDoc doc;
        std::string err;
        if (!parseAdvFile(f, doc, err)) {
            std::printf("FAIL parse %s: %s\n", f, err.c_str());
            g_fail++;
            continue;
        }
        Otr::World w;
        CHECK(w.load(doc.map));
        char verr[2048] = {0};
        const int errs = doc.script.validate(verr, sizeof(verr));
        if (errs != 0) std::printf("validate %s:\n%s\n", f, verr);
        CHECK(errs == 0);
        // @about is written on the FIRST level only (02_* is a later level and
        // deliberately has no @about - the menu reads it from the first file).
        if (!std::strstr(f, "02_")) {
            CHECK(!doc.titleSv.empty());
            CHECK(!doc.titleEn.empty());
        }
    }
    // The Byn map carries 3 hidden treasures that must round-trip through @treasures.
    {
        AdvDoc doc;
        std::string err;
        CHECK(parseAdvFile("docs/adventures/generated/byn_01_byn.adv", doc, err));
        CHECK(doc.map.treasureCount == 3);
    }
}

// --- 3. @resources + @treasures + carry/reset demo (A3/A4/A5) --------------------------------
static void testResourcesDemo()
{
    AdvDoc doc;
    std::string err;
    if (!parseAdvFile("docs/adventures/example_resources_demo.adv", doc, err)) {
        std::printf("FAIL parse demo: %s\n", err.c_str());
        g_fail++;
        return;
    }
    // @about explicit section
    CHECK(doc.titleSv == "Stenhuggarens stig");
    CHECK(doc.titleEn == "The stonemason's path");
    CHECK(doc.author == "MyEsp-OnTheRoad");
    CHECK(doc.cover == "cover.png");

    // A5 carry/reset tokens
    CHECK(doc.carry.size() == 4);
    bool hasEnergy = false, hasItems = false, resetSuit = false;
    for (const std::string &s : doc.carry) { if (s == "energy") hasEnergy = true; if (s == "items") hasItems = true; }
    for (const std::string &s : doc.reset) if (s == "suit") resetSuit = true;
    CHECK(hasEnergy);
    CHECK(hasItems);
    CHECK(resetSuit);

    // A4 resources: glyph -> item + cap + image
    const AdvResource *stone = resByGlyph(doc, 's');
    const AdvResource *jug = resByGlyph(doc, 'w');
    CHECK(stone != nullptr);
    CHECK(jug != nullptr);
    CHECK(stone && stone->id == "stone" && stone->cap == 9 && stone->image == "stone.png");
    CHECK(jug && jug->cap == 3 && jug->nameSv == "Vattenkruka");
    // Resources become story items (reuse the item system) -> has:/give/take validate.
    CHECK(doc.script.itemId("stone", 5) >= 0);
    CHECK(doc.script.itemId("waterjug", 8) >= 0);
    // Placements: 3x 's' + 2x 'w' on the map, translated to walkable ground.
    CHECK(doc.resourcePlacements.size() == 5);

    // The map loads (resource glyphs became grass) and the script validates
    // (including has:stone / take stone and world npc_move).
    Otr::World w;
    CHECK(w.load(doc.map));
    char verr[1024] = {0};
    CHECK(doc.script.validate(verr, sizeof(verr)) == 0);

    // A3 treasure: a gem hides under the bush at (7,2); the visible object is the
    // bush, not the gem (the gem is in the hidden layer until the bush is removed).
    CHECK(w.obj(7, 2) == Otr::Obj::BUSH);
    CHECK(w.obj(7, 2) != Otr::Obj::GEM);
}

// --- 4. Treasure reveal mechanic end to end (A3) ---------------------------------------------
static void testTreasureReveal()
{
    // Player next to a bush; a gem hides under the bush. Hitting the bush must
    // break it AND reveal the treasure (EV_TREASURE), leaving a visible gem.
    static const char *rows[] = {
        "Sb..",
        "....",
        "...X",
    };
    static const Otr::Treasure tr[] = {{1, 0, Otr::Obj::GEM, 1}};
    Otr::MapDef def{};
    def.id = 91;
    def.rows = rows;
    def.h = 3;
    def.rules = {8, 20, Otr::LIVES_INFINITE, 3, 4, 3, 1, 2, 1, false, 0};
    def.treasures = tr;
    def.treasureCount = 1;

    Otr::World w;
    CHECK(w.load(def));
    CHECK(w.obj(1, 0) == Otr::Obj::BUSH);     // covered: bush visible, gem hidden
    w.move(Otr::Dir::RIGHT);                   // face the bush (blocked by it)
    const uint32_t ev = w.hit();               // break the bush
    CHECK(ev & Otr::EV_BROKE);
    CHECK(ev & Otr::EV_TREASURE);
    CHECK(w.obj(1, 0) == Otr::Obj::GEM);       // treasure now visible
}

// --- 5. 80x80 map + walkable border (A2) -----------------------------------------------------
static void testBigMapWalkableBorder()
{
    // 80x80, all walkable grass, with S in the top-left corner and an X inside.
    // No wall ring: the border is walkable, but inside() must still block any step
    // off the edge of the map.
    static std::string store[80];
    static const char *rows[80];
    for (int y = 0; y < 80; y++) {
        store[y] = std::string(80, '.');
        rows[y] = store[y].c_str();
    }
    store[0][0] = 'S';
    store[40][40] = 'X';
    for (int y = 0; y < 80; y++) rows[y] = store[y].c_str();

    Otr::MapDef def{};
    def.id = 92;
    def.rows = rows;
    def.h = 80;
    def.rules = {8, 20, Otr::LIVES_INFINITE, 3, 4, 3, 1, 2, 1, false, 0};

    Otr::World w;
    CHECK(w.load(def));
    CHECK(w.width() == 80);
    CHECK(w.height() == 80);
    // Player at the corner (0,0): stepping off the top/left edge is blocked even
    // though there is no wall there, because the cell is outside the map.
    CHECK(w.move(Otr::Dir::UP) == Otr::EV_BLOCKED);
    CHECK(w.move(Otr::Dir::LEFT) == Otr::EV_BLOCKED);
    // Along the walkable border the player can move.
    CHECK(w.move(Otr::Dir::RIGHT) & Otr::EV_MOVED);
    CHECK(w.move(Otr::Dir::DOWN) & Otr::EV_MOVED);

    // A* planner reaches an interior goal on the big map (exercises 64-bit heap key).
    Otr::Planner pl;
    Otr::Path path{};
    const Otr::PlanResult pr = pl.plan(w, 40, 40, Otr::WalkMode::PRO, path);
    CHECK(pr == Otr::PlanResult::OK);
    CHECK(path.len > 0);
}

// The largest supported map: 128x128 loads, plans the longest corner-to-corner route and
// round-trips through a save image. One more row or column is rejected.
static void testMaxMap()
{
    CHECK(Otr::MAX_W == 128 && Otr::MAX_H == 128);
    static std::string store[Otr::MAX_H + 1];
    static const char *rows[Otr::MAX_H + 1];
    for (int y = 0; y <= Otr::MAX_H; y++) {
        store[y] = std::string(Otr::MAX_W + 1, '.');
        rows[y] = store[y].c_str();
    }
    store[0][0] = 'S';
    store[Otr::MAX_H - 1][Otr::MAX_W - 1] = 'X';
    for (int y = 0; y < Otr::MAX_H; y++) store[y].resize(Otr::MAX_W);

    Otr::MapDef def{};
    def.id = 93;
    def.rows = rows;
    def.h = Otr::MAX_H;
    def.rules = {8, 20, Otr::LIVES_INFINITE, 3, 4, 3, 1, 2, 1, false, 0};
    static Otr::World w;
    CHECK(w.load(def));
    CHECK(w.width() == Otr::MAX_W && w.height() == Otr::MAX_H);

    static Otr::Planner pl;
    static Otr::Path path;
    path = Otr::Path{};
    CHECK(pl.plan(w, Otr::MAX_W - 1, Otr::MAX_H - 1, Otr::WalkMode::PRO, path) == Otr::PlanResult::OK);
    CHECK(path.len == (Otr::MAX_W - 1) + (Otr::MAX_H - 1));

    static uint8_t image[200000];
    const size_t n = w.save(image, sizeof(image));
    CHECK(n > (size_t)Otr::MAX_W * Otr::MAX_H * 7 && n < sizeof(image));
    static Otr::World restored;
    CHECK(restored.load(def));
    CHECK(restored.restore(image, n));
    CHECK(restored.width() == Otr::MAX_W && restored.height() == Otr::MAX_H);

    def.h = Otr::MAX_H + 1;
    CHECK(!w.load(def));
    def.h = Otr::MAX_H;
    store[0].push_back('.');
    CHECK(!w.load(def));
    store[0].resize(Otr::MAX_W);
}

// --- 6. @resources glyph-clash and duplicate detection (A4 validate) -------------------------
static void testResourceGlyphClash()
{
    const std::string base =
        "@meta\nid: clash\nworld: 90\nmap_id: 90\n@map\nS.X\n...\n...\n";

    // (a) resource glyph reuses a built-in glyph ('T' = tree) -> must fail.
    writeTmp("/tmp/adv_clash1.adv", base + "@resources\nT : wood : Ved : Wood : 3 :\n");
    {
        AdvDoc d;
        std::string err;
        CHECK(!parseAdvFile("/tmp/adv_clash1.adv", d, err));
    }
    // (b) resource glyph reuses a person digit -> must fail.
    writeTmp("/tmp/adv_clash2.adv", base + "@resources\n5 : wood : Ved : Wood : 3 :\n");
    {
        AdvDoc d;
        std::string err;
        CHECK(!parseAdvFile("/tmp/adv_clash2.adv", d, err));
    }
    // (c) duplicate resource glyph -> must fail.
    writeTmp("/tmp/adv_clash3.adv",
             base + "@resources\nw : water : Vatten : Water : 3 :\nw : wood : Ved : Wood : 3 :\n");
    {
        AdvDoc d;
        std::string err;
        CHECK(!parseAdvFile("/tmp/adv_clash3.adv", d, err));
    }
    // (d) a free glyph is accepted.
    writeTmp("/tmp/adv_clash4.adv", base + "@resources\nw : water : Vatten : Water : 3 : water.png\n");
    {
        AdvDoc d;
        std::string err;
        CHECK(parseAdvFile("/tmp/adv_clash4.adv", d, err));
        CHECK(d.resources.size() == 1);
    }
}

// --- 7. v0.18.0: @dark, flashlight ('f'/@treasures flashlight N), pogo ('j'/pogo N) -----------
static void testDarkParse()
{
    // Multiple @dark rects: one as an inline arg on the header, two as body lines,
    // one clipped to the map bounds and one fully outside (dropped by World::load).
    const std::string body =
        "@meta\nid: darkp\nworld: 90\nmap_id: 90\n"
        "@map\n"
        "S.........\n"
        "..........\n"
        "..........\n"
        "..........\n"
        "........X.\n"
        "@dark 1,1 3,3\n"
        "@dark\n"
        "5,1 7,2\n"
        "8,3 20,20\n";        // x1,y1 outside -> clipped to 9,4 by World::load
    writeTmp("/tmp/adv_dark.adv", body);
    AdvDoc doc;
    std::string err;
    CHECK(parseAdvFile("/tmp/adv_dark.adv", doc, err));
    CHECK(doc.map.darkCount == 3);          // all three rects parsed (clip happens in load)
    Otr::World w;
    CHECK(w.load(doc.map));
    // Rect 1 (1,1)-(3,3): interior is dark.
    CHECK(w.darkCell(2, 2));
    CHECK(w.darkCell(1, 1) && w.darkCell(3, 3));
    CHECK(!w.darkCell(0, 0));                // S cell is outside the rect
    CHECK(!w.darkCell(4, 1));                // between rects
    // Rect 2 (5,1)-(7,2).
    CHECK(w.darkCell(6, 1) && w.darkCell(7, 2));
    // Rect 3 clipped to the map: (8,3) is in-bounds and dark; (9,4) too; nothing out of range.
    CHECK(w.darkCell(8, 3));
    CHECK(w.darkCell(9, 4));

    // 16-rect cap: 20 rects declared, parser keeps at most MAX_DARK.
    std::string capBody = "@meta\nid: darkcap\nworld: 90\nmap_id: 90\n@map\nS.X\n...\n...\n@dark\n";
    for (int i = 0; i < 20; i++) capBody += "0,0 1,1\n";
    writeTmp("/tmp/adv_darkcap.adv", capBody);
    AdvDoc cap;
    CHECK(parseAdvFile("/tmp/adv_darkcap.adv", cap, err));
    CHECK(cap.map.darkCount == Otr::MAX_DARK);

    // A malformed rect (wrong token count) is a parse error.
    writeTmp("/tmp/adv_darkbad.adv",
             "@meta\nid: db\nworld: 90\nmap_id: 90\n@map\nS.X\n...\n...\n@dark\n1,2 3\n");
    AdvDoc bad;
    CHECK(!parseAdvFile("/tmp/adv_darkbad.adv", bad, err));
}

static void testFlashlightPogoParse()
{
    // Glyphs 'f' and 'j' become engine objects FLASH / POGO (level 1 on pickup).
    const std::string body =
        "@meta\nid: fjp\nworld: 90\nmap_id: 90\n"
        "@map\n"
        "S.f.j.X\n"
        ".......\n"
        ".......\n";
    writeTmp("/tmp/adv_fj.adv", body);
    AdvDoc doc;
    std::string err;
    CHECK(parseAdvFile("/tmp/adv_fj.adv", doc, err));
    Otr::World w;
    CHECK(w.load(doc.map));
    CHECK(w.obj(2, 0) == Otr::Obj::FLASH);
    CHECK(w.obj(4, 0) == Otr::Obj::POGO);

    // @treasures ... flashlight N / pogo N: item + amount(=level) round-trip.
    const std::string tbody =
        "@meta\nid: fjt\nworld: 90\nmap_id: 90\n"
        "@map\n"
        "Sb.c.X\n"
        "......\n"
        "......\n"
        "@treasures\n"
        "1 0 flashlight 2\n"
        "3 0 pogo 3\n";
    writeTmp("/tmp/adv_fjt.adv", tbody);
    AdvDoc td;
    CHECK(parseAdvFile("/tmp/adv_fjt.adv", td, err));
    CHECK(td.map.treasureCount == 2);
    bool fl = false, pg = false;
    for (const Otr::Treasure &t : td.treasures) {
        if (t.item == Otr::Obj::FLASH) fl = (t.amount == 2);
        if (t.item == Otr::Obj::POGO) pg = (t.amount == 3);
    }
    CHECK(fl);
    CHECK(pg);
}

int main(int argc, char **argv)
{
    const std::string path =
        argc > 1 ? argv[1] : "docs/adventures/example_joseph_hebron.adv";

    testHebronExample(path);
    testGeneratedAdventures();
    testResourcesDemo();
    testTreasureReveal();
    testBigMapWalkableBorder();
    testMaxMap();
    testResourceGlyphClash();
    testDarkParse();
    testFlashlightPogoParse();

    std::printf("%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
