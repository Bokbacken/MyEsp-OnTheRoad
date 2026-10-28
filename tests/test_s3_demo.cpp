// External S3 integration test. Compile/run command: docs/adventures/s3_demo/README.md.
#include <cctype>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

#include "adv_loader.h"
#include "otr_world.h"

#define CHECK(c) do { if (!(c)) { \
    std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); return false; \
} } while (0)

static std::string readFile(const std::string &path)
{
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

static bool readSolution(const std::string &readme, std::string &commands)
{
    const size_t marker = readme.find("<!-- solution:start -->");
    CHECK(marker != std::string::npos);
    const size_t fence = readme.find("```text\n", marker);
    CHECK(fence != std::string::npos);
    const size_t start = fence + 8;
    const size_t end = readme.find("```", start);
    CHECK(end != std::string::npos);
    const size_t endMarker = readme.find("<!-- solution:end -->", end);
    CHECK(endMarker != std::string::npos && end < endMarker);
    for (size_t i = start; i < end; ++i) {
        const char c = readme[i];
        if (std::isspace(static_cast<unsigned char>(c))) continue;
        CHECK(c == 'U' || c == 'D' || c == 'L' || c == 'R' || c == 'A');
        commands += c;
    }
    CHECK(!commands.empty());
    return true;
}

static Otr::Dir direction(char c)
{
    return c == 'U' ? Otr::Dir::UP : c == 'D' ? Otr::Dir::DOWN :
           c == 'L' ? Otr::Dir::LEFT : Otr::Dir::RIGHT;
}

static bool restoreAndContinue(std::unique_ptr<Otr::World> &world, const Otr::MapDef &map)
{
    std::vector<uint8_t> save(world->saveSize());
    CHECK(world->save(save.data(), save.size()) == save.size());
    auto restored = std::make_unique<Otr::World>();
    CHECK(restored->load(map));
    CHECK(restored->restore(save.data(), save.size()));
    CHECK(restored->playerX() == world->playerX());
    CHECK(restored->playerY() == world->playerY());
    CHECK(restored->facing() == world->facing());
    CHECK(restored->steps() == world->steps());
    CHECK(restored->energy() == world->energy());
    CHECK(restored->maxEnergy() == world->maxEnergy());
    CHECK(restored->suitCharges() == world->suitCharges());
    CHECK(restored->hasSkates() == world->hasSkates());
    CHECK(restored->gems() == world->gems());
    CHECK(restored->exitReached() == world->exitReached());
    CHECK(restored->livesUsed() == world->livesUsed());
    CHECK(restored->leverOnAt(35, 9) == world->leverOnAt(35, 9));
    for (int group = 0; group < Otr::PLATE_GROUPS; ++group)
        CHECK(restored->gateOpenForGroup(group) == world->gateOpenForGroup(group));
    for (int y = 0; y < world->height(); ++y)
        for (int x = 0; x < world->width(); ++x) {
            CHECK(restored->tile(x, y) == world->tile(x, y));
            CHECK(restored->rawObj(x, y) == world->rawObj(x, y));
            CHECK(restored->objAmount(x, y) == world->objAmount(x, y));
            CHECK(restored->hidden(x, y) == world->hidden(x, y));
        }
    world.swap(restored);
    return true;
}

static bool playFile(const std::string &path, const std::string &commands)
{
    auto loaded = std::make_unique<Adv::Loaded>();
    char err[2048] = {};
    if (!Adv::parseFile(path.c_str(), *loaded, err, sizeof err)) {
        std::printf("parseFile %s: %s\n", path.c_str(), err);
        return false;
    }
    CHECK(std::string(loaded->id) == "s3_demo");
    CHECK(loaded->titleSv[0] && loaded->titleEn[0]);
    CHECK(loaded->script.npcCount == 1);
    CHECK(loaded->script.validate(err, sizeof err) == 0);
    auto world = std::make_unique<Otr::World>();
    CHECK(world->load(loaded->map));
    CHECK(world->width() == 49 && world->height() == 21);
    CHECK(world->energy() == 32 && !world->hasSkates());
    CHECK(world->hidden(4, 11) == Otr::Obj::POTION);
    for (int color = 0; color < 6; ++color) {
        CHECK(world->rawObj(4 + 6 * color, 11) ==
              static_cast<Otr::Obj>(int(Otr::Obj::BOX_RED) + color));
        CHECK(world->tile(4 + 6 * color, 12) ==
              static_cast<Otr::Tile>(int(Otr::Tile::PLATE_RED) + color));
        CHECK(!world->gateOpenForGroup(color + 1));
    }

    int iceBefore = 0, iceAfter = 0, belts = 0, skates = 0, suits = 0;
    int hazards[2][3] = {}, pushes[6] = {};
    int activations = 0, openings = 0, closings = 0, potions = 0, exits = 0;
    int restores = 0, gateCrossings = 0, gems = 0;
    for (size_t i = 0; i < commands.size(); ++i) {
        const char command = commands[i];
        const int x = world->playerX(), y = world->playerY();
        const int steps = world->steps(), energy = world->energy();
        const int charges = world->suitCharges();
        const bool skating = world->hasSkates();
        const bool open = world->gateOpenForGroup(6);
        const bool lever = world->leverOnAt(35, 9);
        const Otr::Dir d = direction(command);
        const int tx = x + Otr::dirX(d), ty = y + Otr::dirY(d);
        const Otr::Tile tile = command == 'A' ? Otr::Tile::LEVER : world->tile(tx, ty);
        const Otr::Obj object = command == 'A' ? Otr::Obj::NONE : world->rawObj(tx, ty);
        Otr::MoveTrace trace;
        const uint32_t ev = command == 'A' ? world->activate() : world->move(d, &trace);
        const int delta = world->steps() - steps;
        if (ev & (Otr::EV_DIED | Otr::EV_GAME_OVER | Otr::EV_TALK)) {
            std::printf("unexpected event at command %zu (%c), from %d,%d\n", i, command, x, y);
            return false;
        }
        if (command == 'A') {
            CHECK(ev & Otr::EV_LEVER);
            CHECK(delta == 0 && world->playerX() == x && world->playerY() == y);
            CHECK(world->leverOnAt(35, 9) != lever);
            ++activations;
            const bool plate = world->rawObj(34, 12) == Otr::Obj::BOX_CYAN;
            CHECK(world->gateOpenForGroup(6) == (plate && !lever));
            if (activations <= 2) {
                CHECK(!plate && !world->gateOpenForGroup(6));
                CHECK(!(ev & (Otr::EV_GATE_OPEN | Otr::EV_GATE_CLOSE)));
            } else {
                CHECK(plate);
                CHECK(ev & (lever ? Otr::EV_GATE_CLOSE : Otr::EV_GATE_OPEN));
            }
        } else if (tile == Otr::Tile::LEVER) {
            CHECK(ev & Otr::EV_BLOCKED);
            CHECK(!(ev & Otr::EV_LEVER));
            CHECK(delta == 0 && world->leverOnAt(35, 9) == lever);
        } else {
            if (!(ev & Otr::EV_MOVED))
                std::printf("blocked command %zu (%c), from %d,%d toward %d,%d\n", i, command, x, y, tx, ty);
            CHECK(ev & Otr::EV_MOVED);
            CHECK(!(ev & Otr::EV_BLOCKED));
            CHECK(delta == trace.count && delta >= 1);
        }
        if (tile == Otr::Tile::ICE) {
            if (!skating) {
                CHECK(x == 4 && y == 2 && delta == 5 && world->playerX() == 9);
                for (int n = 0; n < trace.count; ++n)
                    CHECK(trace.cells[n].x == 5 + n && trace.cells[n].y == 2);
                ++iceBefore;
            } else {
                CHECK(delta == 1 && world->playerX() == tx && world->playerY() == ty);
                ++iceAfter;
            }
        }
        if (tile == Otr::Tile::BELT_RIGHT) {
            CHECK(skating && delta == 3 && world->playerX() == 26);
            ++belts;
        }
        if (object == Otr::Obj::SKATES) {
            CHECK(!skating && world->hasSkates());
            ++skates;
        }
        if (object == Otr::Obj::SUIT) {
            CHECK(ev & Otr::EV_SUIT);
            CHECK(charges == 0 && world->suitCharges() == 8);
            CHECK(energy == 25);
            ++suits;
        }
        const int hazard = int(tile) - int(Otr::Tile::POISON);
        if (hazard >= 0 && hazard < 3) {
            const int base = 1 << hazard;
            CHECK(energy - world->energy() == (charges ? base / 2 : base));
            CHECK(bool(ev & Otr::EV_HURT) == bool(charges ? base / 2 : base));
            CHECK(world->suitCharges() == charges);
            ++hazards[charges ? 1 : 0][hazard];
        }
        const int box = int(object) - int(Otr::Obj::BOX_RED);
        if (box >= 0 && box < 6) {
            CHECK(ev & Otr::EV_PUSHED);
            ++pushes[box];
            if (box < 5) {
                CHECK(world->rawObj(4 + 6 * box, 12) == object);
                CHECK(world->gateOpenForGroup(box + 1));
                CHECK(ev & Otr::EV_GATE_OPEN);
            } else if (pushes[5] == 1) {
                CHECK(!lever && !world->gateOpenForGroup(6));
            } else if (pushes[5] == 2) {
                CHECK(lever && !world->gateOpenForGroup(6));
                CHECK(world->rawObj(34, 13) == Otr::Obj::BOX_CYAN);
                CHECK(ev & Otr::EV_GATE_CLOSE);
            } else {
                CHECK(lever && world->gateOpenForGroup(6));
                CHECK(world->rawObj(34, 12) == Otr::Obj::BOX_CYAN);
                CHECK(ev & Otr::EV_GATE_OPEN);
            }
        }
        if (ev & Otr::EV_POTION) {
            CHECK(ev & Otr::EV_TREASURE);
            CHECK(x == 4 && y == 10 && energy == 22 && world->energy() == 28);
            CHECK(world->hidden(4, 11) == Otr::Obj::NONE);
            CHECK(world->rawObj(4, 11) == Otr::Obj::NONE);
            ++potions;
        }
        if (!open && world->gateOpenForGroup(6)) ++openings;
        if (open && !world->gateOpenForGroup(6)) ++closings;
        if (tile == Otr::Tile::GATE) {
            CHECK(world->playerX() == tx && world->playerY() == ty);
            CHECK(world->gateOpenAt(tx, ty));
            ++gateCrossings;
        }
        if (ev & Otr::EV_GEM) ++gems;
        if (ev & Otr::EV_EXIT) ++exits;

        if ((box == 4 && pushes[4] == 1) ||
            (command == 'A' && activations == 3)) {
            CHECK(!world->exitReached());
            CHECK(restoreAndContinue(world, loaded->map));
            ++restores;
        }
    }
    CHECK(iceBefore == 1 && iceAfter == 4 && belts == 1 && skates == 1 && suits == 1);
    for (int protectedState = 0; protectedState < 2; ++protectedState)
        for (int hazard = 0; hazard < 3; ++hazard)
            CHECK(hazards[protectedState][hazard] == 1);
    for (int color = 0; color < 6; ++color) {
        CHECK(pushes[color] == (color == 5 ? 3 : 1));
        CHECK(world->rawObj(4 + 6 * color, 12) ==
              static_cast<Otr::Obj>(int(Otr::Obj::BOX_RED) + color));
        CHECK(world->gateOpenForGroup(color + 1));
    }
    CHECK(activations == 5 && openings == 3 && closings == 2);
    CHECK(potions == 1 && restores == 2 && gateCrossings == 6);
    CHECK(gems == 3 && world->gems() == world->gemsTotal());
    CHECK(exits == 1 && world->exitReached());
    CHECK(commands.size() == 224 && world->steps() == 223);
    CHECK(world->playerX() == 2 && world->playerY() == 18);
    CHECK(world->energy() == 28 && world->maxEnergy() == 32 && world->livesUsed() == 0);
    CHECK(world->hasSkates() && world->suitCharges() == 8 && world->leverOnAt(35, 9));
    CHECK(restoreAndContinue(world, loaded->map));
    CHECK(world->exitReached());
    std::printf("PASS %s: %zu inputs, %d displacements, energy %d, two continued restores\n",
                path.c_str(), commands.size(), world->steps(), world->energy());
    return true;
}

int main()
{
    const std::string docs = "docs/adventures/s3_demo/";
    const std::string sd = "sdcard_bundle/otr/adventure/s3_demo/";
    const std::string readme = readFile(docs + "README.md");
    if (readme.empty() || readFile(docs + "s3_demo.adv").empty() ||
        readme != readFile(sd + "README.md") ||
        readFile(docs + "s3_demo.adv") != readFile(sd + "s3_demo.adv")) {
        std::puts("FAIL: missing or unequal S3 demo mirrors");
        return 1;
    }
    std::string commands;
    if (!readSolution(readme, commands)) return 1;
    return playFile(docs + "s3_demo.adv", commands) &&
           playFile(sd + "s3_demo.adv", commands) ? 0 : 1;
}
