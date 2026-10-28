// g++ -std=c++17 -Wall -Wextra -Icomponents/otr_engine/include tests/test_adv_s4_engine.cpp components/otr_engine/otr_world.cpp -o tests/test_adv_s4_engine && ./tests/test_adv_s4_engine
#include "otr_world.h"
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
using namespace Otr;
static World w, next;
static int passed, failed;
#define CHECK(c) do { if (c) ++passed; else { ++failed; std::printf("FAIL %d: %s\n", __LINE__, #c); } } while (0)
static MapDef map(const char *const *rows, int h, const EnemyDef *e = nullptr, int count = 0)
{
    MapDef d{};
    d.id = 42; d.rows = rows; d.h = h; d.rules = CLASSIC_RULES;
    d.enemies = e; d.enemyCount = count;
    return d;
}
static EnemyDef chaser(int x, int y)
{
    EnemyDef e{};
    e.kind = EnemyKind::CHASER; e.x = x; e.y = y;
    return e;
}
static EnemyDef patrol(int x, int y, int tx, int ty)
{
    EnemyDef e{};
    e.kind = EnemyKind::PATROL; e.x = x; e.y = y; e.routeCount = 2;
    e.route[0] = {(uint8_t)x, (uint8_t)y};
    e.route[1] = {(uint8_t)tx, (uint8_t)ty};
    return e;
}
static std::vector<uint8_t> save()
{
    std::vector<uint8_t> b(w.saveSize());
    CHECK(w.save(b.data(), b.size()) == b.size());
    return b;
}
static void u32(std::vector<uint8_t> &b, size_t at, uint32_t v)
{
    for (int i = 0; i < 4; ++i) b[at + i] = (uint8_t)(v >> (8 * i));
}
static void seal(std::vector<uint8_t> &b)
{
    u32(b, 10, (uint32_t)b.size() - 20);
    uint32_t crc = ~0u;
    for (size_t i = 0; i < b.size() - 4; ++i) {
        crc ^= b[i];
        for (int j = 0; j < 8; ++j) crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
    }
    u32(b, b.size() - 4, ~crc);
}
static void suction()
{
    char row[] = {'S', (char)SUCTION_GLYPH, '.', 'R', '.', 0};
    const char *rows[] = {row};
    CHECK(w.load(map(rows, 1)));
    CHECK(!w.hasSuction() && !w.canPull());
    CHECK(w.move(Dir::RIGHT) & EV_TREASURE);
    CHECK(w.hasSuction());
    w.move(Dir::RIGHT);
    CHECK(w.canPull());
    MoveTrace trace;
    CHECK((w.pull(&trace) & (EV_MOVED | EV_PUSHED)) == (EV_MOVED | EV_PUSHED));
    CHECK(w.playerX() == 1 && w.obj(2, 0) == Obj::ROCK && trace.count == 1);
    CHECK(w.facing() == Dir::RIGHT && w.steps() == 3);
    auto b = save();
    w.setSuction(false);
    CHECK(w.restore(b.data(), b.size()) && w.hasSuction());
    CHECK(next.load(map(rows, 1)));
    next.carryFrom(w);
    CHECK(next.hasSuction());
    CHECK(w.load(map(rows, 1)) && !w.hasSuction());

    const char *crate[] = {".Sc."};
    CHECK(w.load(map(crate, 1)));
    w.setSuction(true); w.move(Dir::RIGHT);
    CHECK(!w.canPull() && (w.pull() & EV_BLOCKED));
    for (int c = 0; c < 6; ++c) {
        char box[] = {'.', 'S', (char)(BOX_GLYPH + c), '.', 0};
        const char *r[] = {box};
        // A wall behind the box lets a blocked move set the facing without pushing.
        box[3] = '#';
        CHECK(w.load(map(r, 1)));
        w.setSuction(true); CHECK(w.move(Dir::RIGHT) & EV_BLOCKED);
        CHECK(w.pull() & EV_PUSHED);
        CHECK(w.obj(1, 0) == (Obj)((int)Obj::BOX_RED + c));
    }
    char forced[] = {'.', (char)BELT_UP_GLYPH, 'S', 'R', '#', 0};
    const char *fr[] = {forced};
    CHECK(w.load(map(fr, 1)));
    w.setSuction(true); w.move(Dir::RIGHT);
    CHECK(w.pull(&trace) & EV_MOVED);
    CHECK(w.playerX() == 1 && trace.count == 1); // Upward belt is blocked by map edge.
    forced[1] = (char)(BELT_UP_GLYPH + 3);
    CHECK(w.load(map(fr, 1)));
    w.setSuction(true); w.move(Dir::RIGHT);
    CHECK(w.pull(&trace) & EV_MOVED);
    CHECK(w.playerX() == 0 && trace.count == 2);
    forced[1] = (char)ICE_GLYPH;
    CHECK(w.load(map(fr, 1)));
    w.setSuction(true); w.move(Dir::RIGHT);
    CHECK(w.pull(&trace) & EV_MOVED);
    CHECK(w.playerX() == 0 && trace.count == 2 && w.facing() == Dir::RIGHT);

    const char *haz[] = {"^SR#"};
    CHECK(w.load(map(haz, 1)));
    w.setSuction(true); w.move(Dir::RIGHT);
    CHECK(w.pull(&trace) & EV_DIED);
    CHECK(w.playerX() == 1 && w.hasSuction() && trace.count == 1);
    const char *water[] = {"~SR#"};
    CHECK(w.load(map(water, 1)));
    w.setSuction(true); w.move(Dir::RIGHT);
    CHECK(!w.canPull());
    w.setBoat(true); CHECK(w.canPull());
    const char *wetOrigin[] = {".S~R#"};
    CHECK(w.load(map(wetOrigin, 1)));
    w.setSuction(true); w.setBoat(true); w.move(Dir::RIGHT); w.move(Dir::RIGHT);
    CHECK(!w.canPull() && (w.pull() & EV_BLOCKED));
    CHECK(w.obj(3, 0) == Obj::ROCK && w.tile(2, 0) == Tile::WATER);
    const char *hotOrigin[] = {".S^R#"};
    CHECK(w.load(map(hotOrigin, 1)));
    w.setSuction(true); w.setSuitCharges(1); w.move(Dir::RIGHT); w.move(Dir::RIGHT);
    CHECK(!w.canPull() && (w.pull() & EV_BLOCKED));

    const char *hidden[] = {".SR#"};
    Treasure t{2, 0, Obj::GEM, 0};
    auto d = map(hidden, 1); d.treasures = &t; d.treasureCount = 1;
    CHECK(w.load(d)); w.setSuction(true); w.move(Dir::RIGHT);
    CHECK(w.pull() & EV_TREASURE);
    CHECK(w.obj(2, 0) == Obj::GEM && w.hidden(2, 0) == Obj::NONE);

    char grouped[] = {'.', (char)(GROUPED_PLATE_GLYPH + 5), 'R', '#',
                      (char)(GROUPED_GATE_GLYPH + 5), 'S', 0};
    const char *gr[] = {grouped, "......"};
    CHECK(w.load(map(gr, 2)));
    CHECK(w.spawnAt(1, 0));
    w.setSuction(true); w.move(Dir::RIGHT);
    CHECK(w.pull() & EV_GATE_OPEN);
    CHECK(w.objAmount(1, 0) == 5 && w.obj(1, 0) == Obj::ROCK);
    CHECK(w.gateOpenForGroup(5));

    const char *blocked[] = {"#SR#"};
    CHECK(w.load(map(blocked, 1)));
    w.setSuction(true); w.move(Dir::RIGHT);
    CHECK(!w.canPull() && (w.pull() & EV_BLOCKED));
    CHECK(w.obj(2, 0) == Obj::ROCK && w.steps() == 0);

    char timedPlate[] = {'^', (char)(GROUPED_PLATE_GLYPH + 5), 'R', '#', 0};
    char timedGate[] = {'S', '.', '.', (char)(GROUPED_GATE_GLYPH + 5), 0};
    const char *tr[] = {timedPlate, timedGate};
    GroupControl control{5, 3};
    auto td = map(tr, 2); td.controls = &control; td.controlCount = 1;
    CHECK(w.load(td));
    w.setSuction(true); w.move(Dir::RIGHT); w.move(Dir::UP); w.move(Dir::RIGHT);
    CHECK(w.pull() & EV_DIED);
    CHECK(w.timerRemaining(5) == 3 && w.gateOpenForGroup(5));
    CHECK(w.obj(1, 0) == Obj::ROCK && w.playerX() == 0 && w.playerY() == 1);
}
static void pullGateSafety()
{
    for (int color = 0; color < 7; ++color) {
        for (int mode = 0; mode < 4; ++mode) {
            char top[] = {(char)GROUPED_GATE_GLYPH, '.',
                          color ? (char)(COLORED_PLATE_GLYPH + color - 1) :
                                  (char)GROUPED_PLATE_GLYPH,
                          color ? (char)(BOX_GLYPH + color - 1) : 'R', '.', 'S', 0};
            char bottom[] = {mode ? (char)LEVER_GLYPH : '.', '.', '.', '.', '.', '.', 0};
            const char *rows[] = {top, bottom};
            EnemyDef e = patrol(5, 1, 4, 1);
            GroupControl control{0, (uint16_t)(mode == 3 ? 4 : 0), mode == 1};
            auto d = map(rows, 2, &e, 1);
            d.controls = &control; d.controlCount = 1;
            CHECK(w.load(d));
            w.setSuction(true);
            w.move(Dir::LEFT);
            CHECK(w.move(Dir::LEFT) & EV_PUSHED); // Put the object onto its plate.
            w.move(Dir::DOWN); w.move(Dir::LEFT); w.move(Dir::LEFT);
            if (mode) {
                CHECK(w.move(Dir::LEFT) & EV_BLOCKED); // Face the lever.
                CHECK(w.activate() & EV_LEVER);
            }
            w.move(Dir::UP);
            CHECK(w.move(Dir::LEFT) & EV_MOVED); // Enter the currently open gate.
            CHECK(w.move(Dir::RIGHT) & EV_MOVED); // Face the plate-covered object.
            CHECK(w.playerX() == 1 && w.gatesOpen());
            if (mode < 2) {
                const auto before = save();
                const uint32_t revision = w.revision();
                MoveTrace trace;
                CHECK(!w.canPull());
                CHECK(w.pull(&trace) == EV_BLOCKED && trace.count == 0);
                CHECK(save() == before && w.revision() == revision);
                CHECK(w.playerX() == 1 && w.gatesOpen());
            } else {
                CHECK(w.canPull());
                CHECK(w.pull() & EV_MOVED);
                CHECK(w.playerX() == 0 && w.obj(1, 0) ==
                      (color ? (Obj)((int)Obj::BOX_RED + color - 1) : Obj::ROCK));
                if (mode == 2) CHECK(w.gatesOpen()); // An OR lever keeps the retreat gate open.
                else CHECK(!w.gatesOpen() && w.timerRemaining(0) == 0);
                // Expiry on the landing retains the legacy timed-gate movement policy.
            }
        }
    }
}
static void enemies()
{
    const char *rows[] = {"S.....", "......", "......"};
    EnemyDef e = chaser(4, 1);
    auto d = map(rows, 3, &e, 1);
    CHECK(w.load(d));
    CHECK(w.enemyCount() == 1 && w.enemyAt(4, 1) == 0 && w.enemyAt(0, 0) == -1);
    e.x = 5; // Definitions are copied, not borrowed.
    CHECK(w.enemyDef(0).x == 4);
    CHECK(w.move(Dir::UP) & EV_BLOCKED);
    w.hit();
    CHECK(w.enemy(0).x == 4 && w.enemy(0).y == 1);
    CHECK(w.move(Dir::DOWN) & EV_MOVED);
    CHECK(w.enemy(0).x == 3 && w.enemy(0).y == 1);
    CHECK(w.move(Dir::UP) & EV_MOVED);
    CHECK(w.enemy(0).x == 3 && w.enemy(0).y == 0); // U before L on equal BFS paths.

    e = chaser(2, 0); d = map(rows, 3, &e, 1);
    CHECK(w.load(d));
    const uint32_t contactRevision = w.revision();
    CHECK(w.move(Dir::RIGHT) & EV_HURT);
    CHECK(w.revision() > contactRevision);
    CHECK(w.energy() == 6 && w.enemy(0).x == 2); // Enemy waits at player contact.
    CHECK(w.move(Dir::RIGHT) & EV_HURT);
    CHECK(w.playerX() == 2 && w.enemyAt(2, 0) == 0 && w.energy() == 4);
    w.setEnergy(1);
    CHECK(w.move(Dir::LEFT) & EV_DIED);
    CHECK(w.playerX() == 0 && w.enemy(0).x == 2);

    EnemyDef two[] = {chaser(0, 1), chaser(2, 1)};
    const char *contact[] = {".S.", "..."};
    CHECK(w.load(map(contact, 2, two, 2)));
    CHECK(w.move(Dir::DOWN) & EV_HURT);
    CHECK(w.energy() == 6); // Two attackers, one contact loss per public action.

    char belt[] = {'S', (char)(BELT_UP_GLYPH + 1), (char)(BELT_UP_GLYPH + 1), '.', '.', 0};
    const char *forced[] = {belt, "....."};
    e = chaser(2, 0);
    CHECK(w.load(map(forced, 2, &e, 1)));
    MoveTrace trace;
    CHECK(w.move(Dir::RIGHT, &trace) & EV_HURT);
    CHECK(w.energy() == 6 && w.playerX() == 3 && trace.count == 3);
    CHECK(w.enemy(0).x == 2); // One enemy turn, second contact suppressed.
    CHECK(w.load(map(forced, 2, &e, 1)));
    w.setEnergy(1);
    CHECK(w.move(Dir::RIGHT, &trace) & EV_DIED);
    CHECK(trace.count == 2 && w.enemy(0).x == 2 && w.playerX() == 0);
    belt[1] = belt[2] = (char)ICE_GLYPH;
    CHECK(w.load(map(forced, 2, &e, 1)));
    CHECK(w.move(Dir::RIGHT, &trace) & EV_HURT);
    CHECK(w.energy() == 6 && w.playerX() == 3 && trace.count == 3);

    e = patrol(4, 1, 1, 1); e.pingPong = true;
    CHECK(w.load(map(rows, 3, &e, 1)));
    for (int i = 0; i < 3; ++i) w.move(i % 2 ? Dir::UP : Dir::DOWN);
    CHECK(w.enemy(0).x == 1 && w.enemy(0).index == 1);
    w.move(Dir::UP);
    CHECK(w.enemy(0).x == 2 && w.enemy(0).direction == -1 && w.enemy(0).index == 0);
    auto b = save();
    w.move(Dir::DOWN);
    CHECK(w.restore(b.data(), b.size()));
    CHECK(w.enemy(0).x == 2 && w.enemy(0).direction == -1);
    auto bad = b; bad[bad.size() - 5] = 0; seal(bad);
    CHECK(!w.restore(bad.data(), bad.size()));
    CHECK(save() == b); // Atomic restore rejection.
    const size_t extension = 16 + 32 + 6 * 3 * 7 + 86 + 1;
    for (size_t offset : {extension, extension + 1, extension + 2, extension + 6,
                          extension + 7, extension + 8}) {
        bad = b; bad[offset] = 255; seal(bad);
        CHECK(!w.restore(bad.data(), bad.size()));
        CHECK(save() == b);
    }

    const char *blocked[] = {"S.....", "..R...", "......"};
    e = patrol(3, 1, 1, 1);
    CHECK(w.load(map(blocked, 3, &e, 1)));
    w.move(Dir::DOWN);
    CHECK(w.enemy(0).x == 3 && w.enemy(0).y == 1); // Blocked next cell waits; no detour.
    CHECK(w.clearObj(2, 1));
    w.move(Dir::UP);
    CHECK(w.enemy(0).x == 2 && w.enemy(0).y == 1); // Resumes the same segment.
    for (char obstruction : {'D', '='}) {
        char line[] = {'.', '.', obstruction, '.', '.', 0};
        const char *r[] = {"S....", line};
        e = patrol(3, 1, 1, 1);
        CHECK(w.load(map(r, 2, &e, 1)));
        w.move(Dir::RIGHT);
        CHECK(w.enemy(0).x == 3 && w.enemy(0).y == 1);
    }
    const char *isolated[] = {"S....", "..#..", "..#..", "..#.."};
    e = patrol(3, 2, 1, 2);
    e.bounded = true; e.x0 = 0; e.x1 = 4; e.y0 = 1; e.y1 = 3;
    CHECK(!w.load(map(isolated, 4, &e, 1))); // Static walls invalidate a segment.

    const char *door[] = {"SKD...", "......"};
    e = chaser(5, 1);
    CHECK(w.load(map(door, 2, &e, 1)));
    w.move(Dir::RIGHT);
    CHECK(w.enemy(0).x == 4);
    CHECK(w.move(Dir::RIGHT) & EV_DOOR);
    CHECK(w.enemy(0).x == 4 && w.enemy(0).y == 0); // Door opening is an enemy turn.
    CHECK(w.playerX() == 1);

    char lever[] = {'S', (char)LEVER_GLYPH, '.', '.', '.', 0};
    const char *lr[] = {lever, "....."};
    e = chaser(4, 1);
    CHECK(w.load(map(lr, 2, &e, 1)));
    w.move(Dir::RIGHT); CHECK(w.activate() & EV_LEVER);
    CHECK(w.enemy(0).x == 3 && w.enemy(0).y == 1);

    const char *push[] = {"SR..", "...."};
    e = chaser(2, 0);
    CHECK(w.load(map(push, 2, &e, 1)));
    CHECK(w.move(Dir::RIGHT) & EV_BLOCKED);
    CHECK(w.obj(1, 0) == Obj::ROCK && w.enemyAt(2, 0) == 0);
    w.setPogo(1);
    CHECK(w.vault() & EV_HURT);
    CHECK(w.playerX() == 2 && w.energy() == 6);

    char danger[] = {'S', '.', '.', '.', '.', 0};
    const char *dr[] = {danger, "..^..", "....."};
    e = chaser(2, 2);
    CHECK(w.load(map(dr, 3, &e, 1)));
    w.move(Dir::RIGHT);
    CHECK(w.enemy(0).x == 1 && w.enemy(0).y == 2);
    const char *ex[] = {"SX...", "....."};
    e = chaser(4, 1);
    CHECK(w.load(map(ex, 2, &e, 1)));
    CHECK(w.move(Dir::RIGHT) & EV_EXIT);
    CHECK(w.enemy(0).x == 4 && w.enemy(0).y == 1);
    w.setPogo(3); w.setSuction(true);
    CHECK(w.move(Dir::LEFT) == EV_NONE && w.hit() == EV_NONE &&
          w.activate() == EV_NONE && w.vault() == EV_NONE && w.pull() == EV_NONE);
    CHECK(w.enemy(0).x == 4 && w.enemy(0).y == 1);
}
static void checkpointAndClosedGate()
{
    const char *cp[] = {"SQ^...", "......"};
    EnemyDef e = patrol(5, 1, 1, 1);
    CHECK(w.load(map(cp, 2, &e, 1)));
    w.setSuction(true); w.setSkates(true);
    CHECK(w.move(Dir::RIGHT) & EV_CHECKPOINT);
    CHECK(w.enemy(0).x == 4);
    CHECK(w.move(Dir::RIGHT) & EV_DIED);
    CHECK(w.playerX() == 1 && w.playerY() == 0);
    CHECK(w.hasSuction() && w.hasSkates() && w.enemy(0).x == 4 &&
          w.enemy(0).index == 1 && w.enemy(0).direction == 1);

    char top[] = {'S', (char)LEVER_GLYPH, '.', 0};
    char middle[] = {'R', (char)GROUPED_GATE_GLYPH, '.', 0};
    const char *rows[] = {top, middle};
    e = patrol(2, 1, 0, 1);
    CHECK(w.load(map(rows, 2, &e, 1)));
    w.move(Dir::RIGHT);
    CHECK(w.activate() & EV_GATE_OPEN);
    CHECK(w.enemy(0).x == 1 && w.enemy(0).y == 1);
    CHECK(w.activate() & EV_GATE_CLOSE);
    CHECK(w.enemy(0).x == 1 && w.enemy(0).y == 1); // Next route cell is still a rock.
    auto b = save();
    CHECK(w.clearObj(0, 1));
    w.move(Dir::DOWN); w.move(Dir::UP);
    CHECK(w.enemy(0).x == 0 && !w.gatesOpen()); // Can leave a gate that closed underneath.
    CHECK(w.restore(b.data(), b.size()));
    CHECK(w.enemy(0).x == 1 && !w.gatesOpen() && w.obj(0, 1) == Obj::ROCK);
    CHECK(w.clearObj(0, 1));
    w.move(Dir::DOWN); // Enemy tries the player's cell and waits.
    CHECK(w.enemy(0).x == 1 && w.energy() == 6);
    w.move(Dir::RIGHT); // Closed gate is a vaultable blocker containing the enemy.
    w.setPogo(1);
    CHECK(w.vault() & EV_MOVED);
    CHECK(w.playerX() == 2 && w.energy() == 6); // Overflown enemy causes no landing contact.
    CHECK(w.enemy(0).x == 0 && !w.gatesOpen());
}
static void terrainAndTurns()
{
    const char *rows[] = {"S......", ".......", "......."};
    EnemyDef e = patrol(5, 1, 5, 2);
    CHECK(w.load(map(rows, 3, &e, 1)));
    w.move(Dir::RIGHT);
    CHECK(w.enemy(0).y == 2);
    w.move(Dir::LEFT);
    CHECK(w.enemy(0).y == 1 && w.enemy(0).index == 0); // Loop wraps without pingpong.

    e = chaser(5, 1);
    e.bounded = true; e.x0 = 3; e.y0 = 0; e.x1 = 6; e.y1 = 2;
    CHECK(w.load(map(rows, 3, &e, 1)));
    w.move(Dir::DOWN);
    CHECK(w.enemy(0).x == 5 && w.enemy(0).y == 1); // Player outside bound: wait.
    for (int glyph : {int(POISON_GLYPH), int(SWAMP_GLYPH), int(SHARP_ROCKS_GLYPH),
                      int('~'), int('^'), int('m'), int('D'), int('R'), int('c'),
                      int('b'), int('1'), int(GROUPED_GATE_GLYPH)}) {
        char middle[] = {'.', '.', (char)glyph, '.', '.', 0};
        const char *r[] = {"S....", middle, "....."};
        e = chaser(2, 2);
        CHECK(w.load(map(r, 3, &e, 1)));
        w.move(Dir::RIGHT);
        CHECK(w.enemy(0).x == 1 && w.enemy(0).y == 2);
        e.x = 2; e.y = 1;
        CHECK(!w.load(map(r, 3, &e, 1))); // Unsafe/solid starts rejected too.
    }
    for (int glyph : {int(ICE_GLYPH), int(BELT_UP_GLYPH + 1), int('k')}) {
        char middle[] = {'.', '.', (char)glyph, '.', '.', 0};
        const char *r[] = {"S....", middle, "....."};
        e = chaser(2, 2);
        CHECK(w.load(map(r, 3, &e, 1)));
        w.move(Dir::RIGHT);
        CHECK(w.enemy(0).x == 2 && w.enemy(0).y == 1); // No forced enemy motion/pickup.
        if (glyph == 'k') CHECK(w.obj(2, 1) == Obj::KEY);
    }
    const char *gate[] = {"S..", ".=.", "..."};
    e = chaser(1, 2);
    CHECK(w.load(map(gate, 3, &e, 1)));
    w.move(Dir::RIGHT);
    CHECK(w.enemy(0).x == 2 && w.enemy(0).y == 2); // R before L around closed gate.

    char belt[] = {'S', (char)(BELT_UP_GLYPH + 1), (char)(BELT_UP_GLYPH + 1), '.', '.', '.', 0};
    const char *forced[] = {belt, "......", "......"};
    e = patrol(5, 2, 0, 2);
    CHECK(w.load(map(forced, 3, &e, 1)));
    CHECK(w.move(Dir::RIGHT) & EV_MOVED);
    CHECK(w.steps() == 3 && w.enemy(0).x == 4); // Three landings, exactly one enemy turn.

    const char *pullRows[] = {".SR#..", "......", "......"};
    e = patrol(5, 2, 0, 2);
    CHECK(w.load(map(pullRows, 3, &e, 1)));
    w.setSuction(true); w.move(Dir::RIGHT);
    CHECK(w.enemy(0).x == 5);
    CHECK(w.pull() & EV_MOVED);
    CHECK(w.enemy(0).x == 4);
    CHECK(w.load(map(pullRows, 3, &e, 1)));
    w.move(Dir::RIGHT); w.setPogo(2);
    CHECK(w.vault() & EV_MOVED);
    CHECK(w.enemy(0).x == 4);

    const char *collision[] = {".SR#", "...."};
    e = chaser(0, 0);
    CHECK(w.load(map(collision, 2, &e, 1)));
    w.setSuction(true); w.move(Dir::RIGHT);
    CHECK(!w.canPull() && (w.pull() & EV_BLOCKED)); // Retreat cell must be enemy-free.
    CHECK(w.playerX() == 1 && w.enemyAt(0, 0) == 0 && w.energy() == 8);
    CHECK(w.obj(2, 0) == Obj::ROCK && w.steps() == 0);
    const char *overlap[] = {".S.R#"};
    e = chaser(2, 0);
    CHECK(w.load(map(overlap, 1, &e, 1)));
    w.setSuction(true); w.move(Dir::RIGHT); w.move(Dir::RIGHT);
    CHECK(!w.canPull() && (w.pull() & EV_BLOCKED)); // Object destination also enemy-free.
    char slide[] = {'.', (char)ICE_GLYPH, 'S', 'R', '#', 0};
    const char *sr[] = {slide};
    e = chaser(0, 0);
    CHECK(w.load(map(sr, 1, &e, 1)));
    w.setSuction(true); w.move(Dir::RIGHT);
    CHECK(w.pull() & EV_HURT); // Later forced landings retain normal contact handling.
    CHECK(w.playerX() == 0 && w.energy() == 6 && w.facing() == Dir::RIGHT);

    const char *many[] = {"S.........", "..........", ".........."};
    EnemyDef defs[MAX_ENEMIES]{};
    for (int i = 0; i < MAX_ENEMIES; ++i) defs[i] = chaser(i + 1, 2);
    CHECK(w.load(map(many, 3, defs, MAX_ENEMIES)));
    w.move(Dir::RIGHT);
    for (int i = 0; i < MAX_ENEMIES; ++i)
        for (int j = 0; j < i; ++j)
            CHECK(w.enemy(i).x != w.enemy(j).x || w.enemy(i).y != w.enemy(j).y);
    auto b = save();
    CHECK(w.restore(b.data(), b.size()));

    std::vector<std::string> big(MAX_H, std::string(MAX_W, '.'));
    std::vector<const char *> pointers;
    big[0][0] = 'S';
    for (const auto &row : big) pointers.push_back(row.c_str());
    e = chaser(MAX_W - 1, MAX_H - 1);
    CHECK(w.load(map(pointers.data(), MAX_H, &e, 1)));
    w.move(Dir::RIGHT);
    CHECK(w.enemy(0).x == MAX_W - 1 && w.enemy(0).y == MAX_H - 2);

    // A gate opening is required at load, but an enemy already on a closing gate
    // remains valid in saves and may walk off it.
    char controlled[] = {'S', (char)LEVER_GLYPH, '.', 0};
    char gateRow[] = {'.', (char)GROUPED_GATE_GLYPH, '.', 0};
    const char *cr[] = {controlled, gateRow, "..."};
    e = chaser(2, 1);
    CHECK(w.load(map(cr, 3, &e, 1)));
    w.move(Dir::RIGHT); w.activate();
    CHECK(w.enemy(0).x == 1 && w.enemy(0).y == 1);
    w.activate();
    b = save();
    CHECK(w.restore(b.data(), b.size()));
}
static void invalid()
{
    const char *rows[] = {"S.#", "...", "~^m"};
    EnemyDef e = chaser(1, 1);
    auto d = map(rows, 3, &e, 1);
    CHECK(w.load(d));
    for (EnemyPoint p : {EnemyPoint{0, 0}, EnemyPoint{2, 0}, EnemyPoint{0, 2},
                        EnemyPoint{1, 2}, EnemyPoint{2, 2}, EnemyPoint{3, 1}}) {
        e.x = p.x; e.y = p.y; CHECK(!w.load(d));
    }
    e = chaser(1, 1); e.kind = (EnemyKind)2; CHECK(!w.load(d));
    e = chaser(1, 1); e.routeCount = 1; CHECK(!w.load(d));
    e = patrol(1, 1, 2, 0); CHECK(!w.load(d));
    e = patrol(1, 1, 2, 1); e.routeCount = MAX_ROUTE + 1; CHECK(!w.load(d));
    e = chaser(1, 1); e.bounded = true; e.x0 = 2; e.x1 = 1; CHECK(!w.load(d));
    e = chaser(1, 1); e.bounded = true; e.x1 = 0; e.y1 = 0; CHECK(!w.load(d));
    d.enemyCount = MAX_ENEMIES + 1; CHECK(!w.load(d));
    d.enemyCount = 1; d.enemies = nullptr; CHECK(!w.load(d));
    EnemyDef pair[] = {chaser(1, 1), chaser(1, 1)};
    CHECK(!w.load(map(rows, 3, pair, 2)));
    const char *open[] = {"S....", ".....", "....."};
    e = patrol(1, 1, 3, 1);
    e.routeCount = 3; e.route[2] = {3, 2};
    CHECK(!w.load(map(open, 3, &e, 1))); // Diagonal loop closing segment.
    e.pingPong = true;
    CHECK(w.load(map(open, 3, &e, 1)));
    e.route[1] = {2, 2};
    CHECK(!w.load(map(open, 3, &e, 1))); // Diagonal intermediate segment.
}
static void legacy()
{
    const char *rows[] = {"S..."};
    EnemyDef e = chaser(3, 0);
    const auto d = map(rows, 1, &e, 1);
    for (int version = 1; version <= 5; ++version) {
        CHECK(w.load(d));
        // Real historical wire layout: 16-byte header, 32-byte player, 7 bytes/cell,
        // v4 color/group/timer extension (86), v5 skates byte, then CRC.
        const size_t ext = version >= 4 ? 86 : 0;
        std::vector<uint8_t> b(16 + 32 + 4 * 7 + ext + (version >= 5 ? 1 : 0) + 4, 0);
        u32(b, 0, World::SAVE_MAGIC); b[4] = version; b[6] = 42; b[7] = 4; b[8] = 1;
        b[16 + 4] = (uint8_t)Dir::RIGHT;
        b[16 + 15] = 8; b[16 + 16] = 8; b[16 + 17] = 255;
        b[16 + 5] = 2;
        if (version >= 3) { b[16 + 26] = 2; b[16 + 27] = 1; }
        if (version >= 5) b[b.size() - 5] = 1;
        seal(b);
        w.setSuction(true); w.move(Dir::RIGHT);
        CHECK(w.enemy(0).x == 2);
        CHECK(w.restore(b.data(), b.size()));
        CHECK(!w.hasSuction() && w.enemy(0).x == 3 && w.enemy(0).index == 0 &&
              w.enemy(0).direction == 1);
        CHECK(w.keys() == 2 && w.flashlight() == (version >= 3 ? 2 : 0));
        CHECK(w.hasSkates() == (version >= 5));
    }
}
int main()
{
    suction(); pullGateSafety(); enemies(); checkpointAndClosedGate(); terrainAndTurns(); invalid(); legacy();
    std::printf("Host memory: World=%zu bytes, EnemyDef=%zu bytes; shared BFS=%zu bytes\n",
                sizeof(World), sizeof(EnemyDef), size_t(MAX_W * MAX_H) * 4);
    std::printf("S4 engine: %d passed, %d failed\n", passed, failed);
    return failed ? 1 : 0;
}
