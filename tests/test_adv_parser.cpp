// Host test for the .adv importer (tools/adv_parse.*). Parses the real example
// (docs/adventures/example_joseph_hebron.adv) into the engine structures, loads
// the map with World::load() and runs Script::validate() - proving the ASCII
// authoring format round-trips into the actual engine.
//
//   g++ -std=c++17 -Wall -Wextra -Itools -Icomponents/otr_engine/include -Icomponents/otr_story/include
//       tests/test_adv_parser.cpp tools/adv_parse.cpp components/otr_engine/otr_world.cpp
//       components/otr_story/otr_story.cpp -o tests/test_adv_parser && ./tests/test_adv_parser
#include <cstdio>
#include <cstring>
#include <string>

#include "adv_parse.h"
#include "otr_story.h"
#include "otr_world.h"

static int g_pass = 0, g_fail = 0;
#define CHECK(c)                                                             \
    do {                                                                     \
        if (c) g_pass++;                                                     \
        else { g_fail++; std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } \
    } while (0)

int main(int argc, char **argv)
{
    const std::string path =
        argc > 1 ? argv[1] : "docs/adventures/example_joseph_hebron.adv";

    AdvDoc doc;
    std::string err;
    if (!parseAdvFile(path, doc, err)) {
        std::printf("parse error: %s\n", err.c_str());
        return 1;
    }

    // Meta parsed
    CHECK(doc.id == "joseph_camp");
    CHECK(doc.world == 4);
    CHECK(doc.mapId == 4);

    // Story tables parsed (camp: jakob, josef, ruben)
    CHECK(doc.script.npcCount == 3);
    CHECK(doc.script.flagCount == 4);
    CHECK(doc.script.counterCount == 1);
    CHECK(doc.script.itemCount == 1);
    CHECK(doc.script.questCount == 2);
    CHECK(doc.script.nodeCount >= 10);

    // The map loads into the real engine
    Otr::World w;
    CHECK(w.load(doc.map));
    CHECK(w.width() == 40);
    CHECK(w.height() == 30);
    CHECK(w.mapId() == 4);

    // People placed from the map digits
    int x = -1, y = -1;
    CHECK(w.findNpc(1, x, y));   // Jakob
    CHECK(w.findNpc(2, x, y));   // Josef
    CHECK(w.findNpc(3, x, y));   // Ruben (blocks the gap)
    CHECK(!w.findNpc(4, x, y));  // camp has no person 4

    // The script passes the engine's own validator
    char verr[1024] = {0};
    const int errs = doc.script.validate(verr, sizeof(verr));
    if (errs != 0) std::printf("validate errors:\n%s\n", verr);
    CHECK(errs == 0);

    // npcByNum maps digits to the parsed npcs
    const Otr::Story::Npc *n3 = doc.script.npcByNum(3);
    CHECK(n3 != nullptr && std::strcmp(n3->id, "ruben") == 0);

    std::printf("%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
