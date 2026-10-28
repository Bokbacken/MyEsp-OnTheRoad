// Host tests for AdvLive (v0.11.1): adventure live snapshot + JSON for the web page /adventure.
//   g++ -std=c++17 -Wall -Wextra -Imain -Itests -Icomponents/otr_engine/include -Icomponents/otr_story/include
//       tests/test_advlive.cpp main/adv_live.cpp components/otr_engine/*.cpp components/otr_story/*.cpp
#include <stdlib.h>
#include <string.h>

#include "adv_live.h"
#include "check.h"
#include "otr_maps.h"
#include "otr_stories.h"

using namespace AdvLive;

namespace {

char g_buf[JSON_CAP];

bool has(const char *s) { return strstr(g_buf, s) != nullptr; }

// Structural check: braces / brackets balance outside strings, strings are closed, no raw control
// characters inside strings.
bool wellFormed(const char *j)
{
    int depth = 0;
    bool inStr = false;
    for (const char *p = j; *p; p++) {
        const unsigned char c = (unsigned char)*p;
        if (inStr) {
            if (c < 0x20) return false;
            if (c == '\\') {
                if (!p[1]) return false;
                p++;
            } else if (c == '"') {
                inStr = false;
            }
            continue;
        }
        if (c == '"') inStr = true;
        else if (c == '{' || c == '[') depth++;
        else if (c == '}' || c == ']') {
            if (--depth < 0) return false;
        }
    }
    return !inStr && depth == 0;
}

uint32_t jsonUint(const char *key)   // First "key":N in g_buf
{
    char k[40];
    snprintf(k, sizeof(k), "\"%s\":", key);
    const char *p = strstr(g_buf, k);
    return p ? (uint32_t)strtoul(p + strlen(k), nullptr, 10) : 0xFFFFFFFFu;
}

Source source(const Otr::World &w, const char *name, const Otr::Story::Script *sc, const Otr::Story::StoryState *st,
              const Otr::Story::Dialog *dl, int talkNum = 0)
{
    return Source{&w, name, sc, st, dl, 0, "Läs %s", "Okej", "Gå vidare", talkNum};
}

void testInactive()
{
    CHECK(!active());
    const size_t n = writeJson(0, g_buf, sizeof(g_buf));
    CHECK(n > 0 && has("\"active\":false"));
    CHECK(wellFormed(g_buf));
    CHECK(writeJson(0, g_buf, 8) == 0);   // Too small
    CHECK(writeBriefJson(g_buf, sizeof(g_buf)) > 0 && has("\"active\":false") && !has("world"));
}

void testVillage()
{
    using namespace Otr;
    World *w = new World;
    CHECK(w->load(WORLDS[2]));
    Story::StoryState st;
    st.clear();
    Story::Dialog dl;

    const uint32_t v0 = version();
    publish(source(*w, "Byn", &Story::VILLAGE_STORY, &st, &dl));
    CHECK(active());
    CHECK(version() == v0 + 1);

    // First poll (no cached map): map included
    CHECK(writeJson(0, g_buf, sizeof(g_buf)) > 0);
    CHECK(wellFormed(g_buf));
    CHECK(has("\"active\":true"));
    CHECK(has("\"world\":\"Byn\""));
    CHECK(has("\"world_id\":3"));
    CHECK(has("\"map\":{\"w\":32,\"h\":22,\"fog\":false"));
    CHECK(has("\"hint\":[]"));
    // Row 0 is all trees ('a' + TREE = 'g'); row 5 has the shepherd in the tree line: terrain grass,
    // object layer without the person
    CHECK(has("\"terrain\":[\"gggggggggggggggggggggggggggggggg\""));
    CHECK(has("\"player\":{\"x\":15,\"y\":18,\"dir\":"));
    CHECK(has("\"npcs\":[{\"num\":1,\"x\":8,\"y\":12,\"look\":0,\"talking\":false,\"name\":\"Bullbert\"}"));
    CHECK(has("\"name\":\"Fårhild\""));
    CHECK(has("\"name\":\"Gamle Brunolf\""));
    CHECK(has("\"items\":[]"));
    CHECK(has("\"dialog\":null"));
    CHECK(has("\"lives\":-1"));
    const uint32_t mr = jsonUint("map_rev");
    CHECK(mr != 0 && mr != 0xFFFFFFFFu);
    // Door at (26, 11) = 'A' + DOOR = 'E'; NPC cells are '.'
    {
        const char *t = strstr(g_buf, "\"obj\":[");
        CHECK(t != nullptr);
        if (t) {
            const char *row = t + 7;
            for (int y = 0; y < 11; y++) row = strchr(row, ',') + 1;
            CHECK(row[1 + 26] == 'E');
            const char *r12 = row;
            r12 = strchr(r12, ',') + 1;
            CHECK(r12[1 + 8] == '.');   // Bullbert's cell
        }
    }

    // Client has the map: not sent again
    CHECK(writeJson(mr, g_buf, sizeof(g_buf)) > 0);
    CHECK(wellFormed(g_buf));
    CHECK(!has("\"map\":"));
    CHECK(jsonUint("map_rev") == mr);

    // Walking does not change the map revision
    w->move(Dir::UP);
    publish(source(*w, "Byn", &Story::VILLAGE_STORY, &st, &dl));
    CHECK(writeJson(mr, g_buf, sizeof(g_buf)) > 0);
    CHECK(!has("\"map\":"));
    CHECK(has("\"player\":{\"x\":15,\"y\":17,\"dir\":\"up\""));

    // A world change (door opened by the story) bumps it and the map comes again
    CHECK(w->clearObj(26, 11));
    publish(source(*w, "Byn", &Story::VILLAGE_STORY, &st, &dl));
    CHECK(writeJson(mr, g_buf, sizeof(g_buf)) > 0);
    CHECK(has("\"map\":{"));
    CHECK(jsonUint("map_rev") == mr + 1);

    // Conversation with the baker: speaker, text, choices
    CHECK(dl.start(Story::VILLAGE_STORY, st, "baker", nullptr));
    publish(source(*w, "Byn", &Story::VILLAGE_STORY, &st, &dl, 1));
    CHECK(writeJson(mr + 1, g_buf, sizeof(g_buf)) > 0);
    CHECK(wellFormed(g_buf));
    CHECK(has("\"dialog\":{\"speaker\":\"Bullbert\",\"narrator\":false,\"look\":0,\"text\":\"Hallå där!"));
    CHECK(has("\"talking\":true,\"name\":\"Bullbert\""));
    CHECK(has("{\"text\":\"Kan jag få en bulle?\",\"locked\":false,\"hint\":\"\"}"));
    CHECK(has("{\"text\":\"Hej då!\",\"locked\":false,\"hint\":\"\"}"));
    CHECK(!has("\"locked\":true"));
    CHECK(has("\"ref\":\"\""));
    dl.end();

    // The shepherd without a bun: the give-bun answer is shown locked with its hint
    CHECK(dl.start(Story::VILLAGE_STORY, st, "shepherd", nullptr));
    publish(source(*w, "Byn", &Story::VILLAGE_STORY, &st, &dl, 2));
    CHECK(writeJson(mr + 1, g_buf, sizeof(g_buf)) > 0);
    CHECK(has("\"speaker\":\"Fårhild\""));
    CHECK(has("\"locked\":true,\"hint\":\"Kräver: bulle\""));
    dl.end();

    // Carried story items are listed with their names
    const int bun = Story::VILLAGE_STORY.itemId("bun", 3);
    CHECK(bun >= 0);
    st.items[bun] = 2;
    publish(source(*w, "Byn", &Story::VILLAGE_STORY, &st, &dl));
    CHECK(writeJson(mr + 1, g_buf, sizeof(g_buf)) > 0);
    CHECK(has("\"items\":[{\"name\":\"Bulle\",\"count\":2}]"));
    CHECK(has("\"dialog\":null"));

    // English names
    Source en = source(*w, "Town", &Story::VILLAGE_STORY, &st, &dl);
    en.lang = 1;
    publish(en);
    CHECK(writeJson(0, g_buf, sizeof(g_buf)) > 0);
    CHECK(has("\"name\":\"Bunbert\"") && has("\"name\":\"Bun\",\"count\":2"));
    CHECK(has("\"world\":\"Town\""));
    CHECK(writeBriefJson(g_buf, sizeof(g_buf)) > 0 && has("\"active\":true") && has("\"world\":\"Town\"}"));
    CHECK(!has("npcs"));

    // Reloaded world (same revision by chance): invalidateMap() forces a new map revision
    CHECK(writeJson(0, g_buf, sizeof(g_buf)) > 0);
    const uint32_t mr2 = jsonUint("map_rev");
    publish(en);
    CHECK(writeJson(0, g_buf, sizeof(g_buf)) > 0 && jsonUint("map_rev") == mr2);
    invalidateMap();
    publish(en);
    CHECK(writeJson(0, g_buf, sizeof(g_buf)) > 0 && jsonUint("map_rev") == mr2 + 1);

    // Leaving the adventure
    const uint32_t v1 = version();
    setInactive();
    CHECK(!active() && version() == v1 + 1);
    setInactive();   // Already inactive: no bump
    CHECK(version() == v1 + 1);
    CHECK(writeJson(0, g_buf, sizeof(g_buf)) > 0 && has("\"active\":false"));
    delete w;
}

void testFog()
{
    using namespace Otr;
    World *w = new World;
    CHECK(w->load(WORLDS[1]));
    publish(source(*w, "Minfältet", nullptr, nullptr, nullptr));
    CHECK(writeJson(0, g_buf, sizeof(g_buf)) > 0);
    CHECK(wellFormed(g_buf));
    CHECK(has("\"fog\":true"));
    CHECK(has("\"terrain\":[\"gggg") && has("?"));   // Border trees revealed, inner fog hidden
    CHECK(!has("\"hint\":[]"));
    CHECK(has("\"npcs\":[]") && has("\"items\":[]") && has("\"lives\":3"));
    {   // Hidden mines never leak: no 'M' (mine) in the object layer
        const char *o = strstr(g_buf, "\"obj\":[");
        const char *e = o ? strstr(o, "]") : nullptr;
        CHECK(o && e);
        bool mine = false;
        for (const char *p = o; p && p < e; p++) mine = mine || *p == 'M';
        CHECK(!mine);
    }
    setInactive();
    delete w;
}

void testSuctionEnemiesDarkness()
{
    using namespace Otr;
    const char *rows[] = {"########", "#S.....#", "#......#", "#......#", "########"};
    EnemyDef foes[] = {{EnemyKind::PATROL, 4, 2}, {EnemyKind::CHASER, 5, 2}};
    foes[0].routeCount = 1; foes[0].route[0] = {4, 2};
    const DarkRect dark{4, 2, 5, 2};
    MapDef def{91, rows, 5, CLASSIC_RULES, nullptr, 0, &dark, 1};
    def.enemies = foes; def.enemyCount = 2;
    World w;
    CHECK(w.load(def));
    w.setSuction(true);
    Source src = source(w, "Pull", nullptr, nullptr, nullptr);
    auto *snap = new Snapshot{};
    fillMap(*snap, src); fillState(*snap, src);
    CHECK(snap->suction && snap->enemyCount == 0);
    CHECK(snap->terrain[2][4] == '#' && snap->obj[2][4] == '.');
    publish(src);
    CHECK(writeJson(0, g_buf, sizeof(g_buf)) > 0 && has("\"suction\":true") && has("\"enemies\":[]"));
    const uint32_t mr = jsonUint("map_rev");
    // Lighting changes without a world edit must invalidate the cached darkness map.
    w.setFlashlight(3);
    publish(src);
    CHECK(writeJson(mr, g_buf, sizeof(g_buf)) > 0 && has("\"map\":"));
    CHECK(jsonUint("map_rev") > mr);
    const uint32_t litRev = jsonUint("map_rev");
    publish(src);
    CHECK(writeJson(litRev, g_buf, sizeof(g_buf)) > 0 && !has("\"map\":"));
    w.move(Dir::RIGHT);
    publish(src);
    CHECK(writeJson(0, g_buf, sizeof(g_buf)) > 0);
    const uint32_t facingRev = jsonUint("map_rev");
    const uint32_t worldRev = w.revision();
    w.move(Dir::UP);   // Wall: only the flashlight direction changes.
    CHECK(w.revision() == worldRev);
    publish(src);
    CHECK(writeJson(facingRev, g_buf, sizeof(g_buf)) > 0 && has("\"map\":"));
    CHECK(jsonUint("map_rev") > facingRev);
    // Actor kinds are dynamic state, not object glyphs.
    def.darks = nullptr; def.darkCount = 0;
    CHECK(w.load(def));
    invalidateMap(); publish(src);
    CHECK(writeJson(0, g_buf, sizeof(g_buf)) > 0);
    CHECK(has("\"enemies\":[{\"x\":4,\"y\":2,\"kind\":\"patrol\"},{\"x\":5,\"y\":2,\"kind\":\"chaser\"}]"));
    CHECK(wellFormed(g_buf));
    w.move(Dir::RIGHT);
    fillState(*snap, src);
    CHECK(snap->enemyCount == 2 && snap->enemies[1].chaser);
    CHECK(snap->enemies[1].x != 5 || snap->enemies[1].y != 2);
    const char *fogRows[] = {"########", "#Sm....#", "#......#", "#......#", "########"};
    def.rows = fogRows; def.rules.fog = true;
    CHECK(w.load(def));
    fillState(*snap, src);
    CHECK(snap->enemyCount == 0);
    const char pickup[] = {'S', char(SUCTION_GLYPH), char(COLORED_DOOR_GLYPH + 3), 0};
    const char *pickupRows[] = {pickup};
    CHECK(w.load(pickupRows, 1));
    fillMap(*snap, src);
    CHECK(snap->obj[0][1] == 'A' + int(Obj::SUCTION));
    snap->active = true; snap->mapRev = 1;
    CHECK(toJson(*snap, 0, g_buf, sizeof(g_buf)) > 0 && wellFormed(g_buf));
    CHECK(has("\\\\"));   // Coloured-door glyph backslash must be escaped in JSON.
    const char plate[] = {'S', char(COLORED_PLATE_GLYPH + 4), '.', 0};
    const char *plateRows[] = {plate};
    EnemyDef plateFoes[] = {{EnemyKind::PATROL, 1, 0}, {EnemyKind::CHASER, 2, 0}};
    plateFoes[0].routeCount = 1; plateFoes[0].route[0] = {1, 0};
    const DarkRect plateDark{2, 0, 2, 0};
    MapDef plateDef{92, plateRows, 1, CLASSIC_RULES, nullptr, 0, &plateDark, 1};
    plateDef.enemies = plateFoes; plateDef.enemyCount = 2;
    CHECK(w.load(plateDef));
    fillMap(*snap, src); fillState(*snap, src);
    CHECK(snap->terrain[0][1] == '~' && snap->terrain[0][2] == '#');
    CHECK(snap->enemyCount == 1 && snap->enemies[0].x == 1 && !snap->enemies[0].chaser);
    CHECK(toJson(*snap, 0, g_buf, sizeof(g_buf)) > 0 && has("\"a~#\""));
    CHECK(has("\"enemies\":[{\"x\":1,\"y\":0,\"kind\":\"patrol\"}]"));
    delete snap;
    setInactive();
}

void testGroupedGates()
{
    using namespace Otr;
    const char row[] = {'S', 'R', (char)(GROUPED_PLATE_GLYPH + 1),
                        (char)(GROUPED_GATE_GLYPH + 1), 0};
    const char *rows[] = {row};
    World w;
    CHECK(w.load(rows, 1));
    Snapshot snapshot{};
    const Source src = source(w, "Groups", nullptr, nullptr, nullptr);
    fillMap(snapshot, src);
    CHECK(snapshot.terrain[0][3] == 'm');
    CHECK(!(snapshot.terrain[0][3] == 'M'));
    w.move(Dir::RIGHT);
    fillMap(snapshot, src);
    CHECK(snapshot.terrain[0][3] == 'M');
    CHECK(!w.gatesOpen());   // The compatibility query remains group 0.
}

// Worst case size: 64 x 64 fog map + every text at its maximum, all characters needing escapes.
void testWorstCase()
{
    Snapshot *s = (Snapshot *)calloc(1, sizeof(Snapshot));
    CHECK(s != nullptr);
    if (!s) return;
    s->active = true;
    s->version = 7;
    s->mapRev = 3;
    s->w = s->h = 64;
    s->fog = true;
    memset(s->terrain, 'a', sizeof(s->terrain));
    memset(s->obj, '.', sizeof(s->obj));
    memset(s->hint, '0', sizeof(s->hint));
    memset(s->world, '"', sizeof(s->world) - 1);
    s->npcCount = MAX_NPCS;
    for (NpcInfo &n : s->npcs) memset(n.name, '\\', sizeof(n.name) - 1);
    s->itemCount = MAX_ITEMS;
    for (ItemInfo &it : s->items) memset(it.name, '"', sizeof(it.name) - 1);
    s->dialog.active = true;
    memset(s->dialog.speaker, '"', sizeof(s->dialog.speaker) - 1);
    for (int i = 0; i < Otr::Story::MAX_TEXT_CHARS; i++) s->dialog.text[i] = '"';   // 200 characters
    memset(s->dialog.ref, '"', sizeof(s->dialog.ref) - 1);
    s->dialog.choiceCount = MAX_CHOICES;
    for (ChoiceInfo &c : s->dialog.choices) {
        memset(c.text, '"', Otr::Story::MAX_CHOICE_CHARS);
        memset(c.hint, '"', sizeof(c.hint) - 1);
        c.locked = true;
    }
    const size_t n = toJson(*s, 0, g_buf, sizeof(g_buf));
    printf("worst case JSON: %zu bytes (cap %zu)\n", n, JSON_CAP);
    CHECK(n > 15000 && n < JSON_CAP);
    CHECK(wellFormed(g_buf));
    // Escaping of a newline / control character, and the overflow answer
    strcpy(s->dialog.text, "a\"b\\c\nd\x01");
    CHECK(toJson(*s, 3, g_buf, sizeof(g_buf)) > 0);
    CHECK(has("\"text\":\"a\\\"b\\\\c\\nd\\u0001\""));
    CHECK(!has("\"map\":"));
    CHECK(toJson(*s, 0, g_buf, 4000) == 0);
    s->active = false;
    CHECK(toJson(*s, 0, g_buf, sizeof(g_buf)) > 0 && has("{\"active\":false,\"version\":7}"));
    free(s);
}

}  // namespace

int main()
{
    testInactive();
    testVillage();
    testFog();
    testSuctionEnemiesDarkness();
    testGroupedGates();
    testWorstCase();
    CHECK_DONE();
}
