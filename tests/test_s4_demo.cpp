// Production loader/engine integration; the README is the executable solution.
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
    const size_t start = fence + 8, end = readme.find("```", start);
    CHECK(end != std::string::npos);
    CHECK(readme.find("<!-- solution:end -->", end) != std::string::npos);
    for (size_t i = start; i < end; ++i) {
        const char c = readme[i];
        if (std::isspace(static_cast<unsigned char>(c))) continue;
        CHECK(c == 'U' || c == 'D' || c == 'L' || c == 'R' || c == 'A' || c == 'P');
        commands += c;
    }
    CHECK(!commands.empty());
    return true;
}

static bool sameEnemy(const Otr::EnemyState &a, const Otr::EnemyState &b)
{
    return a.x == b.x && a.y == b.y && a.index == b.index && a.direction == b.direction;
}

static bool restoreAndContinue(std::unique_ptr<Otr::World> &world, const Otr::MapDef &map)
{
    std::vector<uint8_t> save(world->saveSize());
    CHECK(world->save(save.data(), save.size()) == save.size());
    auto restored = std::make_unique<Otr::World>();
    CHECK(restored->load(map));
    CHECK(restored->restore(save.data(), save.size()));
    CHECK(restored->playerX() == world->playerX() && restored->playerY() == world->playerY());
    CHECK(restored->facing() == world->facing() && restored->steps() == world->steps());
    CHECK(restored->energy() == world->energy() && restored->maxEnergy() == world->maxEnergy());
    CHECK(restored->hasSuction() == world->hasSuction());
    CHECK(restored->canPull() == world->canPull());
    CHECK(restored->gems() == world->gems() && restored->exitReached() == world->exitReached());
    CHECK(restored->livesUsed() == world->livesUsed());
    CHECK(restored->gateOpenForGroup(1) == world->gateOpenForGroup(1));
    CHECK(restored->leverOnAt(12, 2) == world->leverOnAt(12, 2));
    CHECK(restored->enemyCount() == world->enemyCount());
    for (int i = 0; i < world->enemyCount(); ++i)
        CHECK(sameEnemy(restored->enemy(i), world->enemy(i)));
    for (int y = 0; y < world->height(); ++y)
        for (int x = 0; x < world->width(); ++x) {
            CHECK(restored->tile(x, y) == world->tile(x, y));
            CHECK(restored->rawObj(x, y) == world->rawObj(x, y));
            CHECK(restored->hidden(x, y) == world->hidden(x, y));
            CHECK(restored->objAmount(x, y) == world->objAmount(x, y));
        }
    world.swap(restored);
    return true;
}

static Otr::Dir direction(char c)
{
    return c == 'U' ? Otr::Dir::UP : c == 'D' ? Otr::Dir::DOWN :
           c == 'L' ? Otr::Dir::LEFT : Otr::Dir::RIGHT;
}

static bool playFile(const std::string &path, const std::string &commands)
{
    auto loaded = std::make_unique<Adv::Loaded>();
    char err[2048] = {};
    if (!Adv::parseFile(path.c_str(), *loaded, err, sizeof err)) {
        std::printf("parseFile %s: %s\n", path.c_str(), err);
        return false;
    }
    CHECK(std::string(loaded->id) == "s4_demo");
    CHECK(loaded->titleSv[0] && loaded->titleEn[0]);
    auto world = std::make_unique<Otr::World>();
    CHECK(world->load(loaded->map));
    CHECK(world->width() == 49 && world->height() == 25);
    CHECK(world->energy() == 64 && !world->hasSuction() && !world->canPull());
    CHECK(world->rawObj(4, 3) == Otr::Obj::ROCK);
    CHECK(world->hidden(4, 3) == Otr::Obj::SUCTION);
    CHECK(world->enemyCount() == 4);
    CHECK(world->enemyDef(0).kind == Otr::EnemyKind::PATROL && world->enemyDef(0).bounded);
    CHECK(world->enemyDef(0).pingPong && world->enemyDef(0).routeCount == 2);
    CHECK(world->enemyDef(1).kind == Otr::EnemyKind::CHASER && world->enemyDef(1).bounded);
    CHECK(world->enemyDef(2).kind == Otr::EnemyKind::PATROL && !world->enemyDef(2).bounded);
    CHECK(!world->enemyDef(2).pingPong && world->enemyDef(2).routeCount == 2);
    CHECK(world->enemyDef(3).kind == Otr::EnemyKind::CHASER && world->enemyDef(3).bounded);

    int pickups = 0, pulls = 0, pushes = 0, activations = 0, blocked = 0;
    int openings = 0, closings = 0, restores = 0, replans = 0, patrolWaits = 0, hits = 0;
    bool plugged = false, chaserPlugged = false, chaserReleased = false;
    int chaserWaits = 0;
    for (size_t i = 0; i < commands.size(); ++i) {
        const char c = commands[i];
        const int x = world->playerX(), y = world->playerY(), steps = world->steps();
        const int energy = world->energy();
        const bool open = world->gateOpenForGroup(1), suction = world->hasSuction();
        Otr::EnemyState before[4];
        for (int n = 0; n < 4; ++n) before[n] = world->enemy(n);
        Otr::MoveTrace trace;
        if (c == 'P') {
            CHECK(world->canPull());
            CHECK((world->facing() == Otr::Dir::RIGHT && open &&
                   world->rawObj(10, 4) == Otr::Obj::BOX_RED) ||
                  (world->facing() == Otr::Dir::UP && x == 23 && y == 21 &&
                   world->rawObj(23, 20) == Otr::Obj::BOX_GREEN));
        }
        const uint32_t ev = c == 'P' ? world->pull() :
                            c == 'A' ? world->activate() : world->move(direction(c), &trace);
        if (ev & (Otr::EV_DIED | Otr::EV_GAME_OVER | Otr::EV_TALK)) {
            std::printf("unexpected event at input %zu (%c), from %d,%d\n", i, c, x, y);
            return false;
        }
        if (ev & Otr::EV_BLOCKED) {
            ++blocked;
            CHECK((x == 12 && y == 3 && c == 'U') || (x == 9 && y == 4 && c == 'R') ||
                  (x == 23 && y == 21 && c == 'U'));
            CHECK(world->facing() == direction(c) && world->steps() == steps);
            for (int n = 0; n < 4; ++n) CHECK(sameEnemy(before[n], world->enemy(n)));
        } else if (c == 'A') {
            CHECK(ev & Otr::EV_LEVER);
            CHECK(world->steps() == steps && world->leverOnAt(12, 2));
            CHECK(!world->gateOpenForGroup(1));
            ++activations;
        } else {
            if (!(ev & Otr::EV_MOVED))
                std::printf("no move at input %zu (%c), from %d,%d\n", i, c, x, y);
            CHECK(ev & Otr::EV_MOVED);
            CHECK(world->steps() == steps + 1);
        }
        if (!(ev & Otr::EV_BLOCKED) && !(ev & Otr::EV_EXIT)) {
            const auto &old = before[2];
            const auto &now = world->enemy(2);
            int target = old.index;
            if (old.x == (target == 0 ? 34 : 44)) target = 1 - target;
            const int tx = target == 0 ? 34 : 44;
            CHECK(now.x == old.x + (tx > old.x ? 1 : -1));
            CHECK(now.y == 17 && now.index == target && now.direction == old.direction);
        }
        for (int n = 0; n < 4; ++n) {
            const auto &e = world->enemy(n);
            const auto &def = world->enemyDef(n);
            if (def.bounded)
                CHECK(e.x >= def.x0 && e.x <= def.x1 && e.y >= def.y0 && e.y <= def.y1);
            CHECK(world->rawObj(e.x, e.y) != Otr::Obj::ROCK);
            CHECK(world->rawObj(e.x, e.y) != Otr::Obj::BOX_BLUE);
            CHECK(world->rawObj(e.x, e.y) != Otr::Obj::BOX_GREEN);
        }
        const auto &chaser = world->enemy(1);
        const auto &chaserDef = world->enemyDef(1);
        if (world->playerX() < chaserDef.x0 || world->playerX() > chaserDef.x1 ||
            world->playerY() < chaserDef.y0 || world->playerY() > chaserDef.y1)
            CHECK(sameEnemy(before[1], chaser));
        if (before[1].x == 23 && before[1].y == 13 && chaser.x == 24 && chaser.y == 13) {
            CHECK(world->playerX() == 25 && world->playerY() == 11);
            CHECK(world->rawObj(23, 12) == Otr::Obj::BOX_BLUE);
            ++replans;
            CHECK(restoreAndContinue(world, loaded->map));
            ++restores;
        }
        if (plugged && sameEnemy(before[0], world->enemy(0))) ++patrolWaits;
        if (!suction && world->hasSuction()) {
            CHECK(x == 4 && y == 2 && c == 'D');
            CHECK(ev & Otr::EV_TREASURE);
            CHECK(world->hidden(4, 3) == Otr::Obj::NONE);
            CHECK(world->rawObj(4, 4) == Otr::Obj::ROCK);
            ++pickups;
        }
        if (ev & Otr::EV_HURT) {
            CHECK(energy - world->energy() == 2);
            ++hits;
        }
        if (c == 'P' && y == 4) {
            CHECK(world->playerX() == 8 && world->playerY() == 4);
            CHECK(world->rawObj(9, 4) == Otr::Obj::BOX_RED && !world->gateOpenForGroup(1));
            ++pulls;
            CHECK(restoreAndContinue(world, loaded->map));
            ++restores;
        }
        if (c == 'P' && y == 21) {
            CHECK(world->playerX() == 23 && world->playerY() == 22);
            CHECK(world->rawObj(23, 21) == Otr::Obj::BOX_GREEN);
            chaserPlugged = true;
            ++pulls;
            CHECK(restoreAndContinue(world, loaded->map));
            ++restores;
        } else if (chaserPlugged && !chaserReleased) {
            if (world->rawObj(23, 20) == Otr::Obj::BOX_GREEN) {
                CHECK(x == 23 && y == 22 && c == 'U');
                CHECK(world->enemy(3).x == before[3].x - 1);
                chaserReleased = true;
            } else if (world->playerY() == 21 && world->playerX() < 23) {
                CHECK(sameEnemy(before[3], world->enemy(3)));
                ++chaserWaits;
            }
        }
        if (ev & Otr::EV_PUSHED) ++pushes;
        if (!open && world->gateOpenForGroup(1)) ++openings;
        if (open && !world->gateOpenForGroup(1)) ++closings;
        if (!plugged && world->rawObj(7, 13) == Otr::Obj::ROCK) {
            plugged = true;
            CHECK(x == 7 && y == 11 && c == 'D');
            CHECK(restoreAndContinue(world, loaded->map));
            ++restores;
        }
    }
    CHECK(pickups == 1 && pulls == 2 && activations == 1 && blocked == 3);
    CHECK(pushes == 7 && openings == 2 && closings == 1 && restores == 4);
    CHECK(plugged && patrolWaits > 10 && replans == 1);
    CHECK(chaserPlugged && chaserReleased && chaserWaits >= 3);
    CHECK(world->rawObj(23, 12) == Otr::Obj::BOX_BLUE);
    CHECK(world->rawObj(10, 4) == Otr::Obj::BOX_RED && world->gateOpenForGroup(1));
    CHECK(world->gems() == 3 && world->gems() == world->gemsTotal());
    CHECK(world->exitReached() && world->playerX() == 46 && world->playerY() == 21);
    CHECK(commands.size() == 179 && world->steps() == 175);
    CHECK(world->energy() == 58 && hits == 3 && world->livesUsed() == 0 && world->hasSuction());
    CHECK(restoreAndContinue(world, loaded->map));
    std::printf("PASS %s: %zu inputs, %d displacements, energy %d, hits %d, replan observations %d\n",
                path.c_str(), commands.size(), world->steps(), world->energy(), hits, replans);
    return true;
}

int main()
{
    const std::string docs = "docs/adventures/s4_demo/";
    const std::string sd = "sdcard_bundle/otr/adventure/s4_demo/";
    const std::string readme = readFile(docs + "README.md"), map = readFile(docs + "s4_demo.adv");
    if (readme.empty() || map.empty() || readme != readFile(sd + "README.md") ||
        map != readFile(sd + "s4_demo.adv")) {
        std::puts("FAIL: missing or unequal S4 demo mirrors");
        return 1;
    }
    std::string commands;
    if (!readSolution(readme, commands)) return 1;
    return playFile(docs + "s4_demo.adv", commands) &&
           playFile(sd + "s4_demo.adv", commands) ? 0 : 1;
}
