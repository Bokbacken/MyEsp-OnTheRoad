// Host integration test of the external S2 demo with the production loader/engine.
// Run from the repo root:
//   g++ -std=c++17 -Wall -Wextra -Imain -Icomponents/otr_engine/include
//   -Icomponents/otr_story/include tests/test_s2_demo.cpp main/adv_loader.cpp
//   main/adv_catalog.cpp main/adv_sd_session.cpp
//   components/otr_engine/otr_world.cpp components/otr_story/otr_story.cpp
//   -o tests/test_s2_demo && ./tests/test_s2_demo
#include <cctype>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

#include "adv_loader.h"
#include "adv_sd_session.h"
#include "otr_world.h"

#define CHECK(c) do { if (!(c)) { \
    std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); return false; \
} } while (0)

static std::string readFile(const char *path)
{
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

static bool solutionFromReadme(std::string &commands)
{
    const std::string readme = readFile("docs/adventures/s2_demo/README.md");
    const size_t marker = readme.find("<!-- solution:start -->");
    CHECK(marker != std::string::npos);
    const size_t fence = readme.find("```text\n", marker);
    CHECK(fence != std::string::npos);
    const size_t start = fence + 8;
    const size_t end = readme.find("```", start);
    CHECK(end != std::string::npos);
    CHECK(end < readme.find("<!-- solution:end -->", end));
    for (size_t i = start; i < end; ++i) {
        const char c = readme[i];
        if (std::isspace(static_cast<unsigned char>(c))) continue;
        CHECK(c == 'R' || c == 'U' || c == 'L' || c == 'D' || c == 'A');
        commands += c;
    }
    CHECK(!commands.empty());
    return true;
}

static Otr::Dir direction(char c)
{
    return c == 'R' ? Otr::Dir::RIGHT : c == 'U' ? Otr::Dir::UP :
           c == 'L' ? Otr::Dir::LEFT : Otr::Dir::DOWN;
}

static bool checkSave(const Otr::World &world, const Otr::MapDef &map)
{
    CHECK(Otr::World::SAVE_VERSION == 4);
    std::vector<uint8_t> bytes(world.saveSize());
    CHECK(world.save(bytes.data(), bytes.size()) == bytes.size());
    Otr::World restored;
    CHECK(restored.load(map));
    CHECK(restored.restore(bytes.data(), bytes.size()));
    CHECK(restored.playerX() == world.playerX() && restored.playerY() == world.playerY());
    CHECK(restored.steps() == world.steps() && restored.par() == world.par());
    CHECK(restored.parMet() == world.parMet());
    CHECK(restored.leverOnAt(2, 1) == world.leverOnAt(2, 1));
    CHECK(restored.timerRemaining(2) == world.timerRemaining(2));
    CHECK(restored.gateOpenForGroup(1) == world.gateOpenForGroup(1));
    CHECK(restored.gateOpenForGroup(2) == world.gateOpenForGroup(2));
    for (int color = 0; color < 6; ++color) {
        const auto key = static_cast<Otr::KeyColor>(color);
        CHECK(restored.keys(key) == world.keys(key));
    }
    return true;
}

static bool playFile(const char *path, const std::string &commands, bool wrongKeys)
{
    auto loaded = std::make_unique<Adv::Loaded>();
    char err[2048] = {};
    if (!Adv::parseFile(path, *loaded, err, sizeof err)) {
        std::printf("parseFile %s: %s\n", path, err);
        return false;
    }
    CHECK(std::string(loaded->id) == "s2_demo");
    CHECK(loaded->titleSv[0] && loaded->titleEn[0]);
    CHECK(loaded->descSv[0] && loaded->descEn[0]);
    CHECK(loaded->script.npcCount == 1);
    CHECK(loaded->script.validate(err, sizeof err) == 0);

    Otr::World world;
    CHECK(world.load(loaded->map));
    CHECK(world.width() == 21 && world.height() == 9);
    CHECK(world.par() == 56 && world.steps() == 0 && world.parMet());
    CHECK(!world.gateOpenForGroup(1) && !world.gateOpenForGroup(2));
    CHECK(!world.leverOnAt(2, 1));
    CHECK(world.timerRemaining(2) == 0);
    CHECK((world.move(Otr::Dir::UP) & Otr::EV_TALK) != 0);
    CHECK(world.talkNpc() == 1 && world.steps() == 0);

    int ice = 0, belts[4] = {}, picked[6] = {}, unlocked[6] = {};
    int activations = 0, timedOpened = 0, timedClosed = 0, crossed = 0, exits = 0;
    for (char command : commands) {
        const int x = world.playerX(), y = world.playerY(), steps = world.steps();
        const int timer = world.timerRemaining(2);
        Otr::Tile entered = Otr::Tile::WALL;
        Otr::Obj object = Otr::Obj::NONE;
        int tx = x, ty = y;
        if (command != 'A') {
            const Otr::Dir d = direction(command);
            tx += Otr::dirX(d); ty += Otr::dirY(d);
            entered = world.tile(tx, ty);
            object = world.rawObj(tx, ty);
        }
        const int doorColor = int(object) - int(Otr::Obj::DOOR_RED);
        if (doorColor >= 0 && doorColor < 6 && wrongKeys) {
            // Use the real door and live world, changing only the controlled inventory.
            const auto matching = static_cast<Otr::KeyColor>(doorColor);
            const auto wrong = static_cast<Otr::KeyColor>((doorColor + 1) % 6);
            const int heldMatching = world.keys(matching), heldWrong = world.keys(wrong);
            const int heldGeneric = world.keys();
            CHECK(heldMatching == 1);
            world.setKeys(matching, 0);
            world.setKeys(wrong, 1);
            world.setKeys(1);
            const uint32_t rejected = world.move(direction(command));
            CHECK((rejected & Otr::EV_NEED_KEY) != 0);
            CHECK((rejected & (Otr::EV_DOOR | Otr::EV_MOVED)) == 0);
            CHECK(world.playerX() == x && world.playerY() == y);
            CHECK(world.rawObj(tx, ty) == object);
            CHECK(world.steps() == steps && world.timerRemaining(2) == timer);
            CHECK(world.keys(wrong) == 1 && world.keys(matching) == 0 && world.keys() == 1);
            world.setKeys(matching, heldMatching);
            world.setKeys(wrong, heldWrong);
            world.setKeys(heldGeneric);
        }

        const uint32_t event = command == 'A' ? world.activate() : world.move(direction(command));
        const int delta = world.steps() - steps;
        CHECK((event & (Otr::EV_DIED | Otr::EV_GAME_OVER | Otr::EV_TALK)) == 0);
        if (command == 'A') {
            ++activations;
            CHECK((event & Otr::EV_LEVER) != 0);
            const bool on = activations != 2;
            CHECK(world.leverOnAt(2, 1) == on);
            CHECK(world.gateOpenForGroup(1) == on);
            CHECK((event & (on ? Otr::EV_GATE_OPEN : Otr::EV_GATE_CLOSE)) != 0);
            CHECK(delta == 0);
        } else if (entered == Otr::Tile::LEVER) {
            CHECK((event & Otr::EV_BLOCKED) != 0);
            CHECK((event & Otr::EV_LEVER) == 0);
            CHECK(!world.leverOnAt(tx, ty) && !world.gateOpenForGroup(1));
            CHECK(world.facing() == Otr::Dir::UP && delta == 0);
        } else if (doorColor >= 0 && doorColor < 6) {
            CHECK((event & Otr::EV_DOOR) != 0);
            CHECK((event & Otr::EV_MOVED) == 0);
            CHECK(world.playerX() == x && world.playerY() == y && delta == 0);
            CHECK(world.rawObj(tx, ty) == Otr::Obj::NONE);
            CHECK(world.keys(static_cast<Otr::KeyColor>(doorColor)) == 0);
            ++unlocked[doorColor];
        } else {
            CHECK((event & Otr::EV_MOVED) != 0);
            CHECK((event & Otr::EV_BLOCKED) == 0);
            CHECK(delta > 0);
        }
        if (entered == Otr::Tile::ICE) {
            ++ice;
            CHECK(delta == 4 && world.playerX() == 14 && world.playerY() == 2);
        }
        if (entered >= Otr::Tile::BELT_UP && entered <= Otr::Tile::BELT_LEFT) {
            const int belt = int(entered) - int(Otr::Tile::BELT_UP);
            ++belts[belt];
            CHECK(delta == 2);
            const auto d = static_cast<Otr::Dir>(belt);
            CHECK(world.playerX() == tx + Otr::dirX(d));
            CHECK(world.playerY() == ty + Otr::dirY(d));
        }
        const int keyColor = int(object) - int(Otr::Obj::KEY_RED);
        if (keyColor >= 0 && keyColor < 6) {
            CHECK((event & Otr::EV_KEY) != 0);
            CHECK(world.keys(static_cast<Otr::KeyColor>(keyColor)) == 1);
            CHECK(world.rawObj(tx, ty) == Otr::Obj::NONE);
            ++picked[keyColor];
            CHECK(checkSave(world, loaded->map));
        }
        if (entered == Otr::Tile::PLATE) {
            ++timedOpened;
            CHECK((event & Otr::EV_GATE_OPEN) != 0);
            CHECK(world.gateOpenForGroup(2) && world.timerRemaining(2) == 36);
        } else {
            const int expected = timer > delta ? timer - delta : 0;
            CHECK(world.timerRemaining(2) == expected);
            if (timer > 0 && expected == 0) {
                ++timedClosed;
                CHECK((event & Otr::EV_GATE_CLOSE) != 0);
                CHECK(!world.gateOpenForGroup(2));
            }
        }
        if (entered == Otr::Tile::GATE && world.objAmount(tx, ty) == 2) {
            ++crossed;
            CHECK(world.playerX() == tx && world.playerY() == ty);
            CHECK(world.timerRemaining(2) == 33);
        }
        if (event & Otr::EV_EXIT) ++exits;

        if (world.playerX() == 10 && world.playerY() == 2) {
            CHECK(!Otr::Planner::targetable(world, 11, 2));
            CHECK(!Otr::Planner::targetable(world, 16, 2));
            CHECK(!Otr::Planner::targetable(world, 18, 2));
            CHECK(!Otr::Planner::targetable(world, 18, 4));
            CHECK(!Otr::Planner::targetable(world, 16, 4));
            auto planner = std::make_unique<Otr::Planner>();
            Otr::Path pathResult = {};
            CHECK(planner->plan(world, 15, 2, Otr::WalkMode::ASSIST, pathResult)
                  == Otr::PlanResult::UNREACHABLE);
            const int before = world.timerRemaining(2);
            CHECK(world.activate() == Otr::EV_NONE);
            world.hit();
            CHECK((world.move(Otr::Dir::UP) & Otr::EV_BLOCKED) != 0);
            CHECK(world.steps() == steps + delta && world.timerRemaining(2) == before);
            CHECK(checkSave(world, loaded->map));
        }
    }
    CHECK(activations == 3 && ice == 1);
    CHECK(timedOpened == 1 && timedClosed == 1 && crossed == 1 && exits == 1);
    for (int n : belts) CHECK(n == 1);
    for (int color = 0; color < 6; ++color) {
        CHECK(picked[color] == 1 && unlocked[color] == 1);
        CHECK(world.keys(static_cast<Otr::KeyColor>(color)) == 0);
    }
    CHECK(world.keys() == 0);
    CHECK(world.exitReached() && world.playerX() == 18 && world.playerY() == 7);
    CHECK(world.steps() == 56 && world.par() == 56 && world.parMet());
    CHECK(world.gateOpenForGroup(1) && !world.gateOpenForGroup(2));
    world.move(Otr::Dir::LEFT);
    CHECK(world.steps() == 57 && !world.parMet());
    return true;
}

static bool sessionResume(const std::string &commands)
{
    const char *root = "sdcard_bundle/otr/adventure";
    auto catalog = std::make_unique<Adv::Catalog>();
    CHECK(Adv::scanCatalog(root, *catalog) > 0);
    const Adv::CatalogEntry *entry = nullptr;
    for (int i = 0; i < catalog->count; ++i)
        if (std::string(catalog->items[i].folder) == "s2_demo") entry = &catalog->items[i];
    CHECK(entry && entry->mapCount == 1 && entry->headerOk);

    auto loaded = std::make_unique<Adv::Loaded>();
    Adv::SdSession session;
    Otr::World world;
    Otr::Story::StoryState story;
    const Otr::Story::Script *script = nullptr;
    char err[2048] = {};
    CHECK(session.start(root, *entry, *loaded, world, story, script, err, sizeof err));
    CHECK(script == &loaded->script && script->validate(err, sizeof err) == 0);
    CHECK(!session.completed());

    size_t cursor = 0;
    for (; cursor < commands.size(); ++cursor) {
        const char c = commands[cursor];
        const uint32_t event = c == 'A' ? world.activate() : world.move(direction(c));
        if ((event & Otr::EV_KEY) && world.keys(Otr::KeyColor::GREEN) == 1) {
            ++cursor;
            break;
        }
    }
    CHECK(cursor < commands.size());
    CHECK(world.steps() == 30 && world.timerRemaining(2) == 11);
    CHECK(world.rawObj(12, 5) == Otr::Obj::NONE); // Red door was actually unlocked.
    CHECK(world.keys(Otr::KeyColor::RED) == 0 && world.keys(Otr::KeyColor::GREEN) == 1);
    CHECK(world.rawObj(6, 5) == Otr::Obj::KEY_BLUE);
    CHECK(world.rawObj(4, 7) == Otr::Obj::KEY_YELLOW);
    CHECK(world.rawObj(8, 7) == Otr::Obj::KEY_PURPLE);
    CHECK(world.rawObj(12, 7) == Otr::Obj::KEY_CYAN);
    CHECK(world.leverOnAt(2, 1) && world.gateOpenForGroup(2));
    CHECK(!session.completed());

    std::vector<uint8_t> bytes(world.saveSize() + Otr::Story::StoryState::SAVE_BYTES + 1024);
    const size_t written = session.save(world, story, bytes.data(), bytes.size());
    CHECK(written > 0 && written <= bytes.size());
    bytes.resize(written);

    // A genuinely fresh session loads the real file again before overlaying the save.
    auto freshLoaded = std::make_unique<Adv::Loaded>();
    Adv::SdSession freshSession;
    Otr::World freshWorld;
    Otr::Story::StoryState freshStory;
    const Otr::Story::Script *freshScript = nullptr;
    CHECK(freshSession.start(root, *entry, *freshLoaded, freshWorld, freshStory,
                             freshScript, err, sizeof err));
    CHECK(freshWorld.steps() == 0 && freshWorld.timerRemaining(2) == 0);
    CHECK(!freshWorld.leverOnAt(2, 1) && !freshWorld.gateOpenForGroup(2));
    CHECK(freshWorld.rawObj(12, 5) == Otr::Obj::DOOR_RED);
    CHECK(freshWorld.keys(Otr::KeyColor::GREEN) == 0);
    CHECK(!freshSession.completed());

    CHECK(freshSession.restore(bytes.data(), bytes.size(), *freshLoaded, freshWorld,
                               freshStory, freshScript, err, sizeof err));
    CHECK(freshSession.active() && freshSession.mapIndex() == 0);
    CHECK(!freshSession.completed());
    CHECK(freshSession.mapCount() == 1 && !freshSession.hasNext());
    CHECK(std::string(freshSession.folder()) == "s2_demo");
    CHECK(freshScript == &freshLoaded->script && freshScript->validate(err, sizeof err) == 0);
    CHECK(freshWorld.playerX() == world.playerX() && freshWorld.playerY() == world.playerY());
    CHECK(freshWorld.facing() == world.facing());
    CHECK(freshWorld.steps() == 30 && freshWorld.par() == 56 && freshWorld.parMet());
    CHECK(freshWorld.timerRemaining(2) == 11);
    CHECK(freshWorld.leverOnAt(2, 1) && freshWorld.gateOpenForGroup(1));
    CHECK(freshWorld.gateOpenForGroup(2));
    for (int color = 0; color < 6; ++color) {
        const auto key = static_cast<Otr::KeyColor>(color);
        CHECK(freshWorld.keys(key) == world.keys(key));
    }
    for (int y = 0; y < world.height(); ++y)
        for (int x = 0; x < world.width(); ++x) {
            CHECK(freshWorld.tile(x, y) == world.tile(x, y));
            CHECK(freshWorld.rawObj(x, y) == world.rawObj(x, y));
            CHECK(freshWorld.objAmount(x, y) == world.objAmount(x, y));
        }

    int doors = 0, exits = 0;
    const size_t resumeAt = cursor;
    for (; cursor < commands.size(); ++cursor) {
        const char c = commands[cursor];
        const uint32_t event = c == 'A' ? freshWorld.activate() : freshWorld.move(direction(c));
        CHECK((event & (Otr::EV_NEED_KEY | Otr::EV_BLOCKED | Otr::EV_GAME_OVER)) == 0);
        if (cursor == resumeAt) {
            CHECK(freshWorld.steps() == 31 && freshWorld.timerRemaining(2) == 10);
            CHECK(!freshSession.completed());
        }
        if (event & Otr::EV_DOOR) ++doors;
        if (event & Otr::EV_EXIT) ++exits;
    }
    CHECK(doors == 5 && exits == 1 && freshWorld.exitReached());
    CHECK(freshWorld.playerX() == 18 && freshWorld.playerY() == 7);
    CHECK(freshWorld.steps() == 56 && freshWorld.parMet());
    CHECK(freshWorld.timerRemaining(2) == 0 && !freshWorld.gateOpenForGroup(2));
    for (int color = 0; color < 6; ++color)
        CHECK(freshWorld.keys(static_cast<Otr::KeyColor>(color)) == 0);

    freshSession.markCompleted();
    CHECK(freshSession.completed());
    std::vector<uint8_t> finalBytes(freshWorld.saveSize() + Otr::Story::StoryState::SAVE_BYTES + 1024);
    const size_t finalWritten = freshSession.save(freshWorld, freshStory,
                                                finalBytes.data(), finalBytes.size());
    CHECK(finalWritten > 0 && finalWritten <= finalBytes.size());
    finalBytes.resize(finalWritten);

    auto finalLoaded = std::make_unique<Adv::Loaded>();
    Adv::SdSession finalSession;
    Otr::World finalWorld;
    Otr::Story::StoryState finalStory;
    const Otr::Story::Script *finalScript = nullptr;
    CHECK(finalSession.start(root, *entry, *finalLoaded, finalWorld, finalStory,
                             finalScript, err, sizeof err));
    CHECK(!finalSession.completed() && !finalWorld.exitReached());
    CHECK(finalSession.restore(finalBytes.data(), finalBytes.size(), *finalLoaded,
                               finalWorld, finalStory, finalScript, err, sizeof err));
    CHECK(finalSession.completed() && finalWorld.exitReached() && !finalSession.hasNext());
    CHECK(finalWorld.steps() == 56 && finalWorld.parMet());
    CHECK(finalWorld.timerRemaining(2) == 0 && !finalWorld.gateOpenForGroup(2));
    CHECK(finalWorld.playerX() == 18 && finalWorld.playerY() == 7);
    CHECK(finalScript == &finalLoaded->script && finalScript->validate(err, sizeof err) == 0);
    for (int color = 0; color < 6; ++color)
        CHECK(finalWorld.keys(static_cast<Otr::KeyColor>(color)) == 0);
    return true;
}

int main()
{
    const char *docs = "docs/adventures/s2_demo/s2_demo.adv";
    const char *bundle = "sdcard_bundle/otr/adventure/s2_demo/s2_demo.adv";
    const std::string source = readFile(docs);
    if (source.empty() || source != readFile(bundle)) {
        std::puts("FAIL demo files missing or not byte-identical");
        return 1;
    }
    std::string commands;
    if (!solutionFromReadme(commands) || !playFile(docs, commands, false) ||
        !playFile(bundle, commands, true) || !sessionResume(commands)) return 1;
    std::puts("S2 external demo: both files solved; all mechanics and SD session resume verified");
    return 0;
}
