// Host unit tests for the OnTheRoad Story component (components/otr_story):
//   g++ -std=c++17 -Wall -Wextra -Icomponents/otr_story/include -Itests tests/test_story.cpp components/otr_story/*.cpp
#include <string.h>

#include "check.h"
#include "otr_stories.h"
#include "otr_story.h"

using namespace Otr::Story;

namespace {

struct RecHost : Host {
    char log[512] = {};
    void worldAction(const char *verb, int argc, const int *argv) override
    {
        char b[64];
        snprintf(b, sizeof(b), "%s/%d/%d,%d,%d;", verb, argc, argv[0], argv[1], argv[2]);
        strncat(log, b, sizeof(log) - strlen(log) - 1);
    }
    void sound(const char *name) override
    {
        strncat(log, "snd:", sizeof(log) - strlen(log) - 1);
        strncat(log, name, sizeof(log) - strlen(log) - 1);
        strncat(log, ";", sizeof(log) - strlen(log) - 1);
    }
};

// A small script of our own for the parser tests
const Npc NPCS[] = {{"anna", {"Anna", "Ann"}, 0, 1}, {"bo", {"Bo", nullptr}, 1, 2}};
const char *const FLAGS[] = {"a", "b", "c.d"};
const char *const COUNTERS[] = {"n"};
const Named ITEMS[] = {{"apple", {"Äpple", "Apple"}}, {"key", {"Nyckel", "Key"}}};
const Named QUESTS[] = {{"q", {"Uppdrag", "Quest"}}};
const Node NODES[] = {
    {"anna.start", "a", "anna", {"Igen!", "Again!"}, nullptr, nullptr, nullptr,
     {{{"Hej då", "Bye"}, "END", nullptr, nullptr, {}}}},
    {"anna.start", nullptr, "anna", {"Hej!", "Hi!"}, "1Mo 1:1", nullptr, "set a; add n 2",
     {{{"Dold", "Hidden"}, "END", "b", nullptr, {}},
      {{"Låst", "Locked"}, "anna.x", "has:apple", "take apple", {"Kräver äpple", "Needs apple"}},
      {{"Ta", "Take"}, "anna.x", nullptr, "give apple 3; quest q active; world npc_move 2 5 6; sound talk", {}},
      {{"Slut", "End"}, "END", nullptr, "set b", {}}}},
    {"anna.x", nullptr, nullptr, {"Berättare", nullptr}, nullptr, nullptr, nullptr,
     {{{"Tillbaka", "Back"}, "anna.start", nullptr, nullptr, {}}}},
    {"bo.start", nullptr, "bo", {"Bo här.", nullptr}, nullptr, nullptr, nullptr, {}},
};
const Script SC = {NPCS, 2, NODES, 4, FLAGS, 3, COUNTERS, 1, ITEMS, 2, QUESTS, 1};

void testConditions()
{
    StoryState st;
    st.clear();
    CHECK(evalCond(SC, st, nullptr));
    CHECK(evalCond(SC, st, ""));
    CHECK(!evalCond(SC, st, "a"));
    CHECK(evalCond(SC, st, "!a"));
    CHECK(evalCond(SC, st, " ! a "));
    st.setFlag(0, true);
    st.setFlag(2, true);
    CHECK(evalCond(SC, st, "a & c.d"));
    CHECK(!evalCond(SC, st, "a & b"));
    CHECK(evalCond(SC, st, "a&!b"));
    CHECK(evalCond(SC, st, "n=0 & n>=0 & n<=0"));
    st.counters[0] = 5;
    CHECK(evalCond(SC, st, "n>=5"));
    CHECK(!evalCond(SC, st, "n>=6"));
    CHECK(evalCond(SC, st, "n<=-1") == false);
    st.counters[0] = -3;
    CHECK(evalCond(SC, st, "n<=-1"));
    CHECK(!evalCond(SC, st, "has:apple"));
    st.items[0] = 1;
    CHECK(evalCond(SC, st, "has:apple & !has:key"));
    CHECK(evalCond(SC, st, "quest:q=none"));
    st.quests[0] = (uint8_t)Quest::DONE;
    CHECK(evalCond(SC, st, "quest:q=done"));
    CHECK(!evalCond(SC, st, "quest:q=active"));
    // Broken conditions never hold
    CHECK(!evalCond(SC, st, "nope"));
    CHECK(!evalCond(SC, st, "a & & c.d"));
    CHECK(!evalCond(SC, st, "has:pear"));
    CHECK(!evalCond(SC, st, "n>x"));
    CHECK(!evalCond(SC, st, "n>=abc"));
    CHECK(!evalCond(SC, st, "quest:q=maybe"));
    CHECK(!evalCond(SC, st, "quest:q"));
    CHECK(!evalCond(SC, st, "&"));
}

void testEffects()
{
    StoryState st;
    st.clear();
    RecHost h;
    Note notes[8];
    int nc = 0;
    CHECK(applyEffects(SC, st, "set a; set b ;clear b; add n 5; add n -2", &h, notes, &nc, 8));
    CHECK(st.flag(0) && !st.flag(1));
    CHECK(st.counters[0] == 3);
    CHECK(applyEffects(SC, st, "cnt n 40000", &h));
    CHECK(st.counters[0] == COUNTER_LIMIT);
    CHECK(!applyEffects(SC, st, "add n -9999999", &h));   // Out of the parse range
    CHECK(applyEffects(SC, st, "add n -99999", &h));
    CHECK(st.counters[0] == -COUNTER_LIMIT);
    CHECK(applyEffects(SC, st, "cnt n -32001", &h));
    CHECK(st.counters[0] == -COUNTER_LIMIT);
    CHECK(nc == 0);
    CHECK(applyEffects(SC, st, "give apple; give key 2; take apple 5; take key 9", &h, notes, &nc, 8));
    CHECK(nc == 4);
    CHECK(notes[0].kind == NoteKind::ITEM_GOT && notes[0].id == 0 && notes[0].amount == 1);
    CHECK(notes[1].kind == NoteKind::ITEM_GOT && notes[1].id == 1 && notes[1].amount == 2);
    CHECK(notes[2].kind == NoteKind::ITEM_LOST && notes[2].amount == 1);   // Only had 1
    CHECK(notes[3].kind == NoteKind::ITEM_LOST && notes[3].amount == 2);
    CHECK(st.items[0] == 0 && st.items[1] == 0);
    nc = 0;
    CHECK(applyEffects(SC, st, "take apple", &h, notes, &nc, 8));   // Nothing to take: no note
    CHECK(nc == 0);
    CHECK(applyEffects(SC, st, "give apple 255; give apple 9", &h));
    CHECK(st.items[0] == 255);
    CHECK(applyEffects(SC, st, "quest q active; quest q active; quest q done", &h, notes, &nc, 8));
    CHECK(nc == 2);
    CHECK(notes[0].kind == NoteKind::QUEST_STARTED && notes[1].kind == NoteKind::QUEST_DONE);
    nc = 0;
    CHECK(applyEffects(SC, st, "give key; give key; give key", &h, notes, &nc, 2));   // Cap
    CHECK(nc == 2);
    CHECK(applyEffects(SC, st, "world open 3 4; world keys 1; world quake; sound talk", &h));
    CHECK(strcmp(h.log, "open/2/3,4,0;keys/1/1,0,0;quake/0/0,0,0;snd:talk;") == 0);
    // Syntax errors
    CHECK(!applyEffects(SC, st, "jump a", &h));
    CHECK(!applyEffects(SC, st, "set", &h));
    CHECK(!applyEffects(SC, st, "set zz", &h));
    CHECK(!applyEffects(SC, st, "set a;;set b", &h));
    CHECK(!applyEffects(SC, st, "add n", &h));
    CHECK(!applyEffects(SC, st, "give apple 0", &h));
    CHECK(!applyEffects(SC, st, "give apple 256", &h));
    CHECK(!applyEffects(SC, st, "quest q later", &h));
    CHECK(!applyEffects(SC, st, "world", &h));
    CHECK(!applyEffects(SC, st, "world open 1 2 3 4", &h));
    CHECK(!applyEffects(SC, st, "world open x", &h));
    CHECK(!applyEffects(SC, st, "world verylongverbname1", &h));
    CHECK(!applyEffects(SC, st, "sound", &h));
    CHECK(applyEffects(SC, st, nullptr, &h));
    CHECK(applyEffects(SC, st, "set a", nullptr));   // No host: world / sound are skipped
}

void testDialog()
{
    StoryState st;
    st.clear();
    RecHost h;
    Dialog d;
    CHECK(!d.active());
    CHECK(!d.start(SC, st, "nobody", &h));
    CHECK(!d.active());
    CHECK(d.start(SC, st, "anna", &h));
    CHECK(d.active());
    CHECK(strcmp(d.node()->text.get(0), "Hej!") == 0);   // First visit (flag a not set yet)
    CHECK(strcmp(d.node()->text.get(1), "Hi!") == 0);
    CHECK(d.speaker() == &NPCS[0]);
    CHECK(st.flag(0) && st.counters[0] == 2);            // Node effects on entering
    // "Dold" is hidden (b not set), "Låst" shown disabled (hint), "Ta" and "Slut" enabled
    CHECK(d.choiceCount() == 3);
    CHECK(strcmp(d.choice(0).text.get(0), "Låst") == 0);
    CHECK(!d.enabled(0) && d.enabled(1) && d.enabled(2));
    CHECK(!d.enabled(3) && !d.enabled(-1));
    CHECK(!d.choose(0));                                 // Locked: nothing happens
    CHECK(!d.choose(7));
    CHECK(st.items[0] == 0);
    CHECK(d.choose(1));                                  // "Ta"
    CHECK(st.items[0] == 3 && st.quests[0] == (uint8_t)Quest::ACTIVE);
    CHECK(d.noteCount() == 2);
    CHECK(d.note(0).kind == NoteKind::ITEM_GOT && d.note(0).amount == 3);
    CHECK(d.note(1).kind == NoteKind::QUEST_STARTED);
    CHECK(strcmp(h.log, "npc_move/3/2,5,6;snd:talk;") == 0);
    CHECK(d.active());
    CHECK(d.speaker() == nullptr);                       // Narrator
    CHECK(strcmp(d.node()->text.get(1), "Berättare") == 0);   // Missing English -> Swedish
    CHECK(d.choose(0));                                  // Back to anna.start: now variant 1
    CHECK(strcmp(d.node()->text.get(0), "Igen!") == 0);
    CHECK(d.noteCount() == 0);
    CHECK(d.choose(0));                                  // END
    CHECK(!d.active());
    CHECK(!d.choose(0));
    // Second start: variant "a" again
    CHECK(d.start(SC, st, "anna", &h));
    CHECK(strcmp(d.node()->text.get(0), "Igen!") == 0);
    d.end();
    CHECK(!d.active());
    // A node without choices (the UI offers its own "OK")
    CHECK(d.start(SC, st, "bo", &h));
    CHECK(d.choiceCount() == 0);
    CHECK(strcmp(d.speaker()->name.get(1), "Bo") == 0);
}

void testState()
{
    StoryState st;
    st.clear();
    st.setFlag(0, true);
    st.setFlag(255, true);
    st.setFlag(256, true);   // Ignored
    st.setFlag(-1, true);
    CHECK(st.flag(0) && st.flag(255) && !st.flag(1) && !st.flag(256) && !st.flag(-1));
    st.setFlag(0, false);
    CHECK(!st.flag(0));
    st.counters[3] = -1234;
    st.counters[15] = 32000;
    st.items[31] = 7;
    st.quests[2] = (uint8_t)Quest::DONE;
    CHECK(StoryState::SAVE_BYTES == 128);
    uint8_t buf[200];
    CHECK(st.save(buf, 10) == 0);
    CHECK(st.save(buf, sizeof(buf)) == StoryState::SAVE_BYTES);
    StoryState r;
    r.clear();
    CHECK(r.restore(buf, StoryState::SAVE_BYTES));
    CHECK(memcmp(&r, &st, sizeof(st)) == 0);
    CHECK(!r.restore(buf, StoryState::SAVE_BYTES - 1));
    CHECK(!r.restore(nullptr, StoryState::SAVE_BYTES));
    // Every single bit flip is caught
    int caught = 0;
    for (size_t i = 0; i < StoryState::SAVE_BYTES; i++) {
        buf[i] ^= 0x10;
        StoryState t;
        t.clear();
        caught += !t.restore(buf, StoryState::SAVE_BYTES);
        buf[i] ^= 0x10;
    }
    CHECK(caught == (int)StoryState::SAVE_BYTES);
    CHECK(r.restore(buf, StoryState::SAVE_BYTES));
}

void testValidate()
{
    char err[1024];
    CHECK(SC.validate(err, sizeof(err)) == 0);
    CHECK(err[0] == 0);
    const int village = VILLAGE_STORY.validate(err, sizeof(err));
    if (village) printf("%s", err);
    CHECK(village == 0);

    static const Npc badNpcs[] = {{"x", {"X", nullptr}, 0, 1}, {"y", {"Y", nullptr}, 0, 1}, {"z", {nullptr, nullptr}, 0, 10}};
    static const Node badNodes[] = {
        {"x.start", "nope", "ghost", {"", nullptr}, nullptr, nullptr, "jump",
         {{{"A", nullptr}, "missing", "has:pear", "give pear", {"hint", nullptr}},
          {{nullptr, nullptr}, nullptr, nullptr, nullptr, {}},
          {{"C", nullptr}, "END", nullptr, nullptr, {"hint without cond", nullptr}},
          {{"Det här svarsalternativet är alldeles för långt", nullptr}, "END", nullptr, nullptr, {}}}},
        {"lonely", nullptr, nullptr, {"Ingen når mig", nullptr}, nullptr, nullptr, nullptr, {}},
    };
    static const Script bad = {badNpcs, 3, badNodes, 2, FLAGS, 3, COUNTERS, 1, ITEMS, 2, QUESTS, 1};
    const int n = bad.validate(err, sizeof(err));
    CHECK(n >= 12);
    CHECK(strstr(err, "used twice") != nullptr);
    CHECK(strstr(err, "1..9") != nullptr);
    CHECK(strstr(err, "bad condition 'nope'") != nullptr);
    CHECK(strstr(err, "bad effects 'jump'") != nullptr);
    CHECK(strstr(err, "unknown speaker") != nullptr);
    CHECK(strstr(err, "no text") != nullptr);
    CHECK(strstr(err, "unknown node 'missing'") != nullptr);
    CHECK(strstr(err, "after an empty slot") != nullptr);
    CHECK(strstr(err, "too long") != nullptr);
    CHECK(strstr(err, "hint but no condition") != nullptr);
    CHECK(strstr(err, "no node 'y.start'") != nullptr);
    CHECK(strstr(err, "lonely: unreachable") != nullptr);
    // A tiny buffer is never overrun and still counts everything
    char tiny[8];
    memset(tiny, 'Z', sizeof(tiny));
    CHECK(bad.validate(tiny, 6) == n);
    CHECK(tiny[6] == 'Z' && tiny[7] == 'Z' && strlen(tiny) < 6);
    CHECK(bad.validate(nullptr, 0) == n);
}

void testVillageStory()
{
    const Script &v = VILLAGE_STORY;
    StoryState st;
    st.clear();
    RecHost h;
    Dialog d;
    CHECK(v.npcCount == 3);
    CHECK(v.npcByNum(1) && v.npcByNum(2) && v.npcByNum(3) && !v.npcByNum(4));
    // The shepherd: the bun choice is locked with a hint before you have one
    CHECK(d.start(v, st, "shepherd", &h));
    CHECK(d.choiceCount() == 3 && !d.enabled(0));
    CHECK(d.choice(0).hint.get(0)[0] != 0);
    CHECK(d.choose(2));
    CHECK(!d.active());
    // The baker gives the bun and the quest
    CHECK(d.start(v, st, "baker", &h));
    CHECK(d.choose(0));                                   // "Kan jag få en bulle?"
    CHECK(st.items[0] == 1);
    CHECK(st.quests[0] == (uint8_t)Quest::ACTIVE);
    CHECK(d.noteCount() == 2);
    CHECK(d.choose(0));
    CHECK(!d.active());
    // Back at the shepherd: second-visit variant, bun enabled
    CHECK(d.start(v, st, "shepherd", &h));
    CHECK(d.enabled(0));
    CHECK(d.choose(0));
    CHECK(st.items[0] == 0 && st.quests[0] == (uint8_t)Quest::DONE);
    CHECK(strstr(h.log, "snd:quest;") != nullptr);
    CHECK(d.choose(0));                                   // -> narrator: she moves
    CHECK(d.speaker() == nullptr);
    CHECK(strstr(h.log, "npc_move/3/2,16,6;") != nullptr);
    CHECK(d.choose(0));
    CHECK(!d.active());
    // Brunolf: the joke is locked (friends 3 needed), the riddle has 4 answers
    CHECK(d.start(v, st, "old", &h));
    CHECK(d.choose(0));                                   // -> offer
    CHECK(d.choose(0));                                   // -> riddle
    CHECK(d.choiceCount() == 4);
    CHECK(d.node()->ref != nullptr);
    CHECK(d.choose(0));                                   // Wrong
    CHECK(d.choose(0));                                   // Guess again
    CHECK(d.choose(1));                                   // A towel
    CHECK(strstr(h.log, "keys/1/1,0,0;") != nullptr);
    CHECK(d.choose(0));
    CHECK(d.start(v, st, "old", &h));                     // Solved variant, joke now open
    CHECK(d.enabled(0));
    CHECK(st.counters[0] >= 3);
}

void testUtf8()
{
    CHECK(utf8Len(nullptr) == 0);
    CHECK(utf8Len("") == 0);
    CHECK(utf8Len("abc") == 3);
    CHECK(utf8Len("Fårhild") == 7);
    CHECK(utf8Len("åäö ÅÄÖ") == 7);
    const Text t = {"sv", nullptr};
    CHECK(strcmp(t.get(1), "sv") == 0);
    CHECK(strcmp(t.get(5), "sv") == 0);
    const Text e = {nullptr, nullptr};
    CHECK(strcmp(e.get(0), "") == 0);
}

}  // namespace

int main()
{
    testConditions();
    testEffects();
    testDialog();
    testState();
    testValidate();
    testVillageStory();
    testUtf8();
    CHECK_DONE();
}
