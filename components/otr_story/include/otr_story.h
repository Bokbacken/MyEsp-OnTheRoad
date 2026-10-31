// otr_story.h - OnTheRoad Story: dialogues with people (pure C++, host tested, no LVGL / ESP-IDF).
//
// See docs/BIBLE_ADVENTURE_PLAN.md (steg A). A Script is a set of constant tables (flash):
//   Npc      a person: id ("ruben"), display name, look (sprite / portrait index) and the map
//            number (digit '1'..'9' in the world rows) that places it.
//   Node     one line of a person (or the narrator): id, start condition, text, optional Bible
//            reference / quote key, effects on entering, and up to 4 choices.
//            Several nodes may share an id: the first whose condition holds is used. That is how a
//            person "remembers": "bagaren.start ? !met" and "bagaren.start ? met" are two variants.
//   Choice   answer text, target node id ("END" = conversation over), condition, effects and a
//            hint. A choice whose condition fails is hidden, or shown greyed out with the hint
//            when the hint is set (the author decides per choice).
//
// Conditions and effects are short texts in the manuscript syntax of the plan, parsed when used
// (a few dozen characters, negligible) and checked by Script::validate():
//   condition  terms joined with '&', each optionally negated with '!':
//                flag                 flag is set
//                has:item             at least one of item
//                counter>=N  counter<=N  counter=N
//                quest:q=none|active|done
//   effects    separated by ';':
//                set f / clear f      flag on / off
//                add c N / cnt c N    counter += N / counter = N (clamped to +-32000)
//                give i [N] / take i [N]
//                quest q active|done|none
//                world VERB [a [b [c]]]   handed to the Host (open door, move person, give key ...)
//                sound NAME               handed to the Host
//
// StoryState holds what the conversations changed (flags, counters, items, quests) and is saved
// with the world (fixed-size block with magic, version and CRC32).
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace Otr {
namespace Story {

constexpr int MAX_FLAGS = 256;
constexpr int MAX_COUNTERS = 16;
constexpr int MAX_ITEMS = 32;
constexpr int MAX_QUESTS = 16;
constexpr int MAX_CHOICES = 4;
constexpr int LANGS = 2;               // 0 = Swedish (base), 1 = English
constexpr int MAX_TEXT_CHARS = 200;    // Node text (characters, not bytes): fits the dialog box
constexpr int MAX_CHOICE_CHARS = 36;   // One button line at font 14
constexpr int16_t COUNTER_LIMIT = 32000;

// One text per language; a missing translation falls back to Swedish.
struct Text {
    const char *s[LANGS];
    const char *get(int lang) const
    {
        return (lang > 0 && lang < LANGS && s[lang]) ? s[lang] : s[0] ? s[0] : "";
    }
};

struct Npc {
    const char *id;
    Text name;
    uint8_t look;   // Sprite / portrait set in the UI
    uint8_t num;    // Map digit 1..9
    uint8_t mapId = 1; // Internal SD map; 1 is the main map.
};

struct Choice {
    Text text;            // text.s[0] == nullptr: unused slot
    const char *next;     // Node id or "END"
    const char *cond;     // nullptr = always
    const char *effects;  // nullptr = none
    Text hint;            // Locked: shown greyed out with this hint; no hint = hidden
};

struct Node {
    const char *id;
    const char *cond;      // Variant condition (nullptr = always)
    const char *speaker;   // Npc id, nullptr = narrator
    Text text;
    const char *ref;       // Bible reference shown under the text ("1Mo 37:15-17"), or nullptr
    const char *quote;     // Quote key ("1Mo40:8") looked up in the local quote file, or nullptr
    const char *effects;   // Applied when the node is entered
    Choice choices[MAX_CHOICES];
};

// Declared names: flags and counters are plain ids, items and quests also have display names.
struct Named {
    const char *id;
    Text name;
    uint8_t cap = 255;   // Resource limits; ordinary story items retain the legacy limit.
};

struct Script {
    const Npc *npcs;
    int npcCount;
    const Node *nodes;
    int nodeCount;
    const char *const *flags;
    int flagCount;
    const char *const *counters;
    int counterCount;
    const Named *items;
    int itemCount;
    const Named *quests;
    int questCount;

    int flagId(const char *name, size_t len) const;      // -1 if not declared
    int counterId(const char *name, size_t len) const;
    int itemId(const char *name, size_t len) const;
    int questId(const char *name, size_t len) const;
    const Npc *npc(const char *id) const;
    const Npc *npcByNum(int num, int mapId = 1) const;

    // Checks the whole script: declared names, condition / effect syntax, every target exists,
    // every node reachable from a "<npc>.start", text lengths, speakers, npc numbers unique.
    // Returns the number of errors; the first ones are written to err (one per line).
    int validate(char *err, size_t cap) const;
};

enum class Quest : uint8_t { NONE, ACTIVE, DONE };

struct StoryState {
    uint8_t flags[MAX_FLAGS / 8];
    int16_t counters[MAX_COUNTERS];
    uint8_t items[MAX_ITEMS];
    uint8_t quests[MAX_QUESTS];

    void clear();
    bool flag(int id) const { return id >= 0 && id < MAX_FLAGS && (flags[id >> 3] >> (id & 7)) & 1; }
    void setFlag(int id, bool on);

    static constexpr uint32_t MAGIC = 0x5452544F;   // "OTRT"
    static constexpr uint16_t VERSION = 1;
    static constexpr size_t SAVE_BYTES = 12 + MAX_FLAGS / 8 + 2 * MAX_COUNTERS + MAX_ITEMS + MAX_QUESTS + 4;
    size_t save(uint8_t *buf, size_t cap) const;      // SAVE_BYTES, or 0 if cap is too small
    bool restore(const uint8_t *buf, size_t n);       // Validates; untouched on error
};

// Receives the effects that belong to the world / the device.
class Host {
public:
    virtual ~Host() = default;
    virtual void worldAction(const char *verb, int argc, const int *argv) = 0;
    virtual bool worldActionOnMap(const char *verb, int mapId, int argc, const int *argv)
    {
        if (mapId != 1) return false;
        worldAction(verb, argc, argv);
        return true;
    }
    virtual void worldTravel(const char *destination) { (void)destination; }
    virtual void sound(const char *name) = 0;
};

// What a choice changed (for toasts): up to MAX_NOTES per step.
enum class NoteKind : uint8_t { ITEM_GOT, ITEM_LOST, QUEST_STARTED, QUEST_DONE };
struct Note {
    NoteKind kind;
    uint8_t id;      // Item / quest id
    int16_t amount;  // Items
};

bool evalCond(const Script &sc, const StoryState &st, const char *cond);
// Applies effects; notes (optional) collects changes. Returns false on a syntax error (the
// effects before the error stay applied - validate() keeps that from happening in practice).
bool applyEffects(const Script &sc, StoryState &st, const char *effects, Host *host, Note *notes = nullptr,
                  int *noteCount = nullptr, int noteCap = 0);

class Dialog {
public:
    static constexpr int MAX_NOTES = 8;

    // Starts the conversation with npc (first matching "<npc>.start"). False if none matches.
    bool start(const Script &sc, StoryState &st, const char *npcId, Host *host);
    bool active() const { return node_ != nullptr; }
    void end() { node_ = nullptr; }
    const Node *node() const { return node_; }
    const Npc *speaker() const;   // nullptr = narrator

    // Visible choices of the current node (hidden ones are skipped).
    int choiceCount() const { return count_; }
    const Choice &choice(int i) const { return node_->choices[slot_[i]]; }
    bool enabled(int i) const { return i >= 0 && i < count_ && enabled_[i]; }

    // Picks visible choice i: applies its effects and goes on. Returns false (and changes
    // nothing) for an invalid or disabled choice. After it, active() tells if the talk goes on.
    bool choose(int i);

    // Changes of the last start() / choose().
    int noteCount() const { return noteCount_; }
    const Note &note(int i) const { return notes_[i]; }

private:
    bool enter(const char *id);
    const Script *sc_ = nullptr;
    StoryState *st_ = nullptr;
    Host *host_ = nullptr;
    const Node *node_ = nullptr;
    uint8_t slot_[MAX_CHOICES] = {};
    bool enabled_[MAX_CHOICES] = {};
    int count_ = 0;
    Note notes_[MAX_NOTES] = {};
    int noteCount_ = 0;
};

// Number of characters (UTF-8 code points) in s.
int utf8Len(const char *s);

}  // namespace Story
}  // namespace Otr
