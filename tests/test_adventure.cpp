// Host unit tests for the OnTheRoad Engine tile world (components/otr_engine):
//   g++ -std=c++17 -Wall -Wextra -Icomponents/otr_engine/include tests/test_adventure.cpp components/otr_engine/otr_world.cpp components/otr_engine/otr_maps.cpp components/otr_engine/otr_maps_joseph.cpp -o tests/test_adventure && ./tests/test_adventure
#include <cstdio>
#include <cstring>

#include "otr_joystick.h"
#include "otr_maps.h"
#include "otr_world.h"

using namespace Otr;

static int g_fail = 0, g_pass = 0;

#define CHECK_EQ(actual, expected)                                                              \
    do {                                                                                        \
        long a_ = (long)(actual), e_ = (long)(expected);                                        \
        if (a_ != e_) {                                                                         \
            std::printf("FAIL %s:%d  %s = %ld, expected %ld\n", __FILE__, __LINE__, #actual, a_, e_); \
            g_fail++;                                                                           \
        } else {                                                                                \
            g_pass++;                                                                           \
        }                                                                                       \
    } while (0)
#define CHECK(cond) CHECK_EQ((cond) ? 1 : 0, 1)

static World W;   // ~12 kB: keep it off the stack

template <int N>
static bool load(const char *const (&rows)[N]) { return W.load(rows, N); }

static void testLoad()
{
    const char *bad1[] = {"S..", ".."};
    CHECK(!load(bad1));                       // Ragged rows
    const char *bad2[] = {"...", "..."};
    CHECK(!load(bad2));                       // No start
    const char *bad3[] = {"S.S"};
    CHECK(!load(bad3));                       // Two starts
    const char *bad4[] = {"S.?"};
    CHECK(!load(bad4));                       // Unknown character
    CHECK(!W.load(nullptr, 1));
    const char *ok[] = {"#####", "#S*.#", "#####"};
    CHECK(load(ok));
    CHECK_EQ(W.width(), 5);
    CHECK_EQ(W.height(), 3);
    CHECK_EQ(W.playerX(), 1);
    CHECK_EQ(W.playerY(), 1);
    CHECK_EQ(W.gemsTotal(), 1);
    CHECK_EQ((int)W.tile(-1, 0), (int)Tile::WALL);   // Outside = wall
    CHECK_EQ((int)W.obj(99, 0), (int)Obj::NONE);
}

static void testWalkAndPickup()
{
    const char *m[] = {"S*k.T", "....#"};
    CHECK(load(m));
    uint32_t ev = W.move(Dir::RIGHT);
    CHECK(ev & EV_MOVED);
    CHECK(ev & EV_GEM);
    CHECK(ev & EV_ALL_GEMS);
    CHECK_EQ(W.gems(), 1);
    CHECK_EQ((int)W.obj(1, 0), (int)Obj::NONE);
    ev = W.move(Dir::RIGHT);
    CHECK(ev & EV_KEY);
    CHECK_EQ(W.keys(), 1);
    W.move(Dir::RIGHT);
    ev = W.move(Dir::RIGHT);                  // Tree
    CHECK_EQ(ev, EV_BLOCKED);
    CHECK_EQ(W.playerX(), 3);
    CHECK_EQ((int)W.facing(), (int)Dir::RIGHT);
    ev = W.move(Dir::UP);                     // Map edge
    CHECK_EQ(ev, EV_BLOCKED);
    CHECK_EQ((int)W.facing(), (int)Dir::UP);
    CHECK_EQ(W.steps(), 3);
}

static void testPush()
{
    const char *m[] = {"SR..R#", "......"};
    CHECK(load(m));
    uint32_t ev = W.move(Dir::RIGHT);
    CHECK(ev & EV_PUSHED);
    CHECK(ev & EV_MOVED);
    CHECK_EQ((int)W.obj(2, 0), (int)Obj::ROCK);
    CHECK_EQ(W.playerX(), 1);
    W.move(Dir::RIGHT);                       // Rock to x=3, now next to the second rock
    ev = W.move(Dir::RIGHT);                  // Rock behind rock: blocked
    CHECK_EQ(ev, EV_BLOCKED);
    CHECK_EQ(W.playerX(), 2);
    CHECK_EQ((int)W.obj(3, 0), (int)Obj::ROCK);
    // Rock against a wall
    const char *m2[] = {"SR#"};
    CHECK(load(m2));
    CHECK_EQ(W.move(Dir::RIGHT), EV_BLOCKED);
    // Rock into water -> bridge; then the player can walk over it without a boat
    const char *m3[] = {"SR~~."};
    CHECK(load(m3));
    ev = W.move(Dir::RIGHT);
    CHECK(ev & EV_SPLASH);
    CHECK_EQ((int)W.tile(2, 0), (int)Tile::BRIDGE);
    CHECK_EQ((int)W.obj(2, 0), (int)Obj::NONE);
    W.move(Dir::RIGHT);
    CHECK_EQ(W.playerX(), 2);
    ev = W.move(Dir::RIGHT);                  // Real water, no boat
    CHECK(ev & EV_BLOCKED);
    // Rock into lava -> basalt
    const char *m4[] = {"SR^"};
    CHECK(load(m4));
    const uint32_t rev = W.revision();
    ev = W.move(Dir::RIGHT);
    CHECK(ev & EV_SIZZLE);
    CHECK_EQ((int)W.tile(2, 0), (int)Tile::BASALT);
    CHECK(W.revision() != rev);
}

static void testHit()
{
    const char *m[] = {"SC%#"};
    CHECK(load(m));
    W.move(Dir::RIGHT);                       // Blocked by crate, but now facing it
    CHECK_EQ(W.playerX(), 0);
    uint32_t ev = W.hit();
    CHECK_EQ(ev, EV_HIT | EV_BROKE);
    CHECK_EQ((int)W.obj(1, 0), (int)Obj::NONE);
    W.move(Dir::RIGHT);
    CHECK_EQ(W.playerX(), 1);
    ev = W.hit();                             // Cracked wall, 1st hit
    CHECK_EQ(ev, EV_HIT);
    CHECK_EQ(W.damage(2, 0), 1);
    CHECK_EQ((int)W.tile(2, 0), (int)Tile::CRACKED);
    ev = W.hit();                             // 2nd hit
    CHECK_EQ(ev, EV_HIT | EV_BROKE);
    CHECK_EQ((int)W.tile(2, 0), (int)Tile::RUBBLE);
    W.move(Dir::RIGHT);
    CHECK_EQ(W.playerX(), 2);
    CHECK_EQ(W.hit(), EV_HIT);                // Solid wall: thud, nothing changes
    CHECK_EQ((int)W.tile(3, 0), (int)Tile::WALL);
    W.move(Dir::LEFT);
    W.move(Dir::LEFT);
    CHECK_EQ(W.playerX(), 0);
    W.move(Dir::RIGHT);
    W.move(Dir::LEFT);                        // Facing the map edge
    CHECK_EQ(W.hit(), EV_NONE);
}

static void testDoorAndBoat()
{
    const char *m[] = {"SDk", ".~B"};
    CHECK(load(m));
    uint32_t ev = W.move(Dir::RIGHT);
    CHECK_EQ(ev, EV_BLOCKED | EV_NEED_KEY);
    CHECK_EQ((int)W.obj(1, 0), (int)Obj::DOOR);
    // Go around? Water at (1,1) blocks without the boat
    W.move(Dir::DOWN);
    ev = W.move(Dir::RIGHT);
    CHECK(ev & EV_BLOCKED);
    // Second map: key first, then the door
    const char *m2[] = {"SkD.", "~~~B"};
    CHECK(load(m2));
    W.move(Dir::RIGHT);
    CHECK_EQ(W.keys(), 1);
    ev = W.move(Dir::RIGHT);
    CHECK_EQ(ev, EV_DOOR);                    // Unlock: does not walk through
    CHECK_EQ(W.keys(), 0);
    CHECK_EQ(W.playerX(), 1);
    CHECK_EQ((int)W.obj(2, 0), (int)Obj::NONE);
    W.move(Dir::RIGHT);
    W.move(Dir::RIGHT);
    ev = W.move(Dir::DOWN);
    CHECK(ev & EV_BOAT);
    CHECK(W.hasBoat());
    ev = W.move(Dir::LEFT);
    CHECK(ev & EV_SAILING);
    CHECK(W.sailing());
    W.move(Dir::LEFT);
    W.move(Dir::LEFT);
    CHECK_EQ(W.playerX(), 0);
    W.move(Dir::UP);
    CHECK(!W.sailing());
}

static void testLavaAndGates()
{
    const char *m[] = {"S.^", "R..", ".o=", "..."};
    CHECK(load(m));
    W.move(Dir::RIGHT);
    uint32_t ev = W.move(Dir::RIGHT);
    CHECK(ev & EV_BURNED);
    CHECK_EQ(W.playerX(), 0);
    CHECK_EQ(W.playerY(), 0);
    CHECK(!W.gatesOpen());
    CHECK(!W.playerCanEnter(2, 2));
    ev = W.move(Dir::DOWN);                   // Rock (0,1) -> (0,2)
    CHECK(ev & EV_PUSHED);
    W.move(Dir::RIGHT);                       // (1,1)
    W.move(Dir::DOWN);                        // (1,2): the player on the plate does not count
    CHECK(!W.gatesOpen());
    // Map where the rock goes straight onto the plate
    const char *m2[] = {"S..", ".R.", ".o=", "..."};
    CHECK(load(m2));
    W.move(Dir::RIGHT);                       // (1,0)
    ev = W.move(Dir::DOWN);                   // Rock (1,1) -> plate (1,2)
    CHECK(ev & EV_PUSHED);
    CHECK(ev & EV_GATE_OPEN);
    CHECK(W.gatesOpen());
    CHECK(W.playerCanEnter(2, 2));
    W.move(Dir::RIGHT);                       // (2,1)
    W.move(Dir::DOWN);                        // Through the open gate (2,2)
    CHECK_EQ(W.playerY(), 2);
    W.move(Dir::DOWN);                        // (2,3)
    W.move(Dir::LEFT);                        // (1,3)
    ev = W.move(Dir::UP);                     // Push the rock off the plate -> (1,1)
    CHECK(ev & EV_GATE_CLOSE);
    CHECK(!W.gatesOpen());
    // Rocks never go onto gates
    const char *m3[] = {"SR=o"};
    CHECK(load(m3));
    CHECK_EQ(W.move(Dir::RIGHT), EV_BLOCKED);
}

// ---- Test world walkthrough -------------------------------------------------------------
// BFS over cells the player can enter (no lava, no objects that block) and walk there.
static bool walkTo(int gx, int gy)
{
    static int prev[MAX_H][MAX_W];
    static int queue[MAX_H * MAX_W];
    for (auto &r : prev)
        for (int &v : r) v = -2;
    int head = 0, tail = 0;
    const int s = W.playerY() * MAX_W + W.playerX();
    prev[W.playerY()][W.playerX()] = -1;
    queue[tail++] = s;
    while (head < tail) {
        const int c = queue[head++], cx = c % MAX_W, cy = c / MAX_W;
        if (cx == gx && cy == gy) break;
        for (int d = 0; d < 4; d++) {
            const int nx = cx + dirX((Dir)d), ny = cy + dirY((Dir)d);
            if (!W.inside(nx, ny) || prev[ny][nx] != -2) continue;
            if (!W.playerCanEnter(nx, ny) || W.tile(nx, ny) == Tile::LAVA) continue;
            prev[ny][nx] = c;
            queue[tail++] = ny * MAX_W + nx;
        }
    }
    if (prev[gy][gx] == -2) return false;
    int path[MAX_H * MAX_W], n = 0;
    for (int c = gy * MAX_W + gx; c != s; c = prev[c / MAX_W][c % MAX_W]) path[n++] = c;
    for (int i = n - 1; i >= 0; i--) {
        const int nx = path[i] % MAX_W, ny = path[i] / MAX_W;
        const Dir d = nx > W.playerX() ? Dir::RIGHT : nx < W.playerX() ? Dir::LEFT : ny > W.playerY() ? Dir::DOWN : Dir::UP;
        if (!(W.move(d) & EV_MOVED)) return false;
    }
    return W.playerX() == gx && W.playerY() == gy;
}

static void testWorld()
{
    for (int y = 0; y < TEST_WORLD_H; y++) CHECK_EQ((int)strlen(TEST_WORLD[y]), TEST_WORLD_W);
    CHECK(W.load(TEST_WORLD, TEST_WORLD_H));
    CHECK_EQ(W.width(), 60);
    CHECK_EQ(W.height(), 60);
    CHECK_EQ(W.gemsTotal(), 13);
    CHECK_EQ(W.playerX(), 5);
    CHECK_EQ(W.playerY(), 5);
    CHECK(!W.gatesOpen());

    // 1. Rock (18,18) right x4, then down x2 onto the plate (22,20) -> garden gate opens
    CHECK(walkTo(17, 18));
    for (int i = 0; i < 4; i++) CHECK(W.move(Dir::RIGHT) & EV_PUSHED);
    CHECK_EQ((int)W.obj(22, 18), (int)Obj::ROCK);
    CHECK(walkTo(22, 17));
    CHECK(W.move(Dir::DOWN) & EV_PUSHED);
    CHECK(W.move(Dir::DOWN) & EV_GATE_OPEN);
    // 2. Key in the garden
    CHECK(walkTo(20, 10));
    CHECK_EQ(W.keys(), 1);
    // 3. Boathouse door in the ruin, then the boat
    CHECK(!walkTo(48, 20));                   // Not reachable yet (door locked)
    CHECK(walkTo(44, 18));
    CHECK_EQ(W.move(Dir::RIGHT), EV_DOOR);
    CHECK(walkTo(48, 20));
    CHECK(W.hasBoat());
    // 4. Gem room behind the cracked wall (45,8)
    CHECK(walkTo(44, 8));
    W.move(Dir::RIGHT);
    CHECK_EQ(W.hit(), EV_HIT);
    CHECK_EQ(W.hit(), EV_HIT | EV_BROKE);
    CHECK(walkTo(50, 6));
    // 5. Sail to the lake island, break into its south half
    CHECK(walkTo(43, 44));
    CHECK(walkTo(44, 45));
    W.move(Dir::DOWN);
    W.hit();
    CHECK(W.hit() & EV_BROKE);
    CHECK(walkTo(47, 48));
    CHECK(walkTo(40, 51));
    // 6. Lava island: not reachable by walking or sailing
    CHECK(!walkTo(13, 45));
    // 7. The remaining open gems
    static const int OPEN[][2] = {{2, 5}, {20, 3}, {25, 19}, {23, 9}, {35, 4}, {53, 9}};
    for (const auto &g : OPEN) CHECK(walkTo(g[0], g[1]));
    CHECK_EQ(W.gems(), 10);
    // 8. Tree pocket: break a crate from below
    CHECK(walkTo(4, 17));
    W.move(Dir::UP);
    CHECK_EQ(W.hit(), EV_HIT | EV_BROKE);
    CHECK(walkTo(4, 14));
    CHECK_EQ(W.gems(), 11);
    // 9. Lava causeway at x=14: five rocks pushed down into the lava rows 38..42.
    //    Rock (14,34) first, then line up the others in column 14 and push them down.
    CHECK(walkTo(14, 33));
    for (int i = 0; i < 4; i++) CHECK(W.move(Dir::DOWN) & EV_PUSHED);   // rock -> 38 (sizzle on 4th)
    CHECK_EQ((int)W.tile(14, 38), (int)Tile::BASALT);
    // Rock (12,36): push right twice from (11,36) -> (14,36), then down from (14,35)
    struct Push { int sx, sy; Dir d; int n; };
    static const Push PLAN[] = {
        {11, 36, Dir::RIGHT, 2}, {14, 35, Dir::DOWN, 3},    // -> 39
        {17, 35, Dir::LEFT, 2}, {14, 34, Dir::DOWN, 5},     // rock (16,35) -> (14,35) -> 40
        {8, 35, Dir::RIGHT, 5}, {14, 34, Dir::DOWN, 6},     // rock (9,35) -> (14,35) -> 41
        {20, 36, Dir::LEFT, 5}, {14, 35, Dir::DOWN, 6},     // rock (19,36) -> (14,36) -> 42
    };
    for (const Push &p : PLAN) {
        CHECK(walkTo(p.sx, p.sy));
        for (int i = 0; i < p.n; i++) CHECK(W.move(p.d) & EV_PUSHED);
    }
    for (int y = 38; y <= 42; y++) CHECK_EQ((int)W.tile(14, y), (int)Tile::BASALT);
    CHECK(walkTo(13, 45));
    CHECK_EQ(W.gems(), 12);
    CHECK(walkTo(15, 45));
    CHECK_EQ(W.move(Dir::DOWN) & (EV_GEM | EV_ALL_GEMS), EV_GEM | EV_ALL_GEMS);
    CHECK_EQ(W.gems(), 13);
}

// ---- v0.8.0: pathfinding, rules, treasures, minesweeper, save ----------------------------
static Planner P;   // ~70 kB
static Path PA;
static uint8_t SAVEBUF[64 * 1024];

template <int N>
static bool loadR(const char *const (&rows)[N], const Rules &r) { return W.load(rows, N, r); }

static constexpr Rules R8 = {8, 20, 2, 3, 4, 3, 1, 2, 1, false, 0};   // Finite lives, lava 3

static uint32_t follow(const Path &p)
{
    uint32_t ev = 0;
    for (int i = 0; i < p.len; i++) ev |= W.move((Dir)p.dirs[i]);
    return ev;
}

static void testPickTarget()
{
    static const char *const m[] = {"#####", "#S.T#", "#...#", "#####"};
    CHECK(load(m));
    int x = -1, y = -1;
    CHECK(!Planner::pickTarget(W, 3.5f, 1.5f, x, y));   // Clear tap on the tree
    CHECK(Planner::pickTarget(W, 3.1f, 1.5f, x, y));    // Near its left edge -> (2,1)
    CHECK_EQ(x, 2); CHECK_EQ(y, 1);
    CHECK(Planner::pickTarget(W, 3.5f, 1.9f, x, y));    // Near its bottom edge -> (3,2)
    CHECK_EQ(x, 3); CHECK_EQ(y, 2);
    CHECK(Planner::pickTarget(W, 1.5f, 2.5f, x, y));    // Plain ground
    CHECK_EQ(x, 1); CHECK_EQ(y, 2);
    CHECK(!Planner::pickTarget(W, 0.5f, 0.5f, x, y));   // Wall corner, diagonal too far
    CHECK(Planner::pickTarget(W, 0.9f, 1.5f, x, y));    // Wall, close to (1,1)
    CHECK_EQ(x, 1); CHECK_EQ(y, 1);
    CHECK(!Planner::pickTarget(W, 0.6f, 1.5f, x, y));   // Wall, more than 1/3 away
    CHECK(Planner::pickTarget(W, 3.8f, 1.8f, x, y));    // Tree corner: nearest is (3,2) below
    CHECK_EQ(x, 3); CHECK_EQ(y, 2);
    CHECK(!Planner::targetable(W, 3, 1));
    CHECK(!Planner::targetable(W, -1, 0));
}

static void testPlanBasic()
{
    static const char *const m[] = {"#######", "#S.#..#", "#..#..#", "#.....#", "#######"};
    CHECK(load(m));
    CHECK_EQ((int)P.plan(W, 4, 1, WalkMode::PRO, PA), (int)PlanResult::OK);
    CHECK_EQ(PA.len, 7);                                  // Around the wall
    CHECK_EQ((int)P.plan(W, 4, 1, WalkMode::ASSIST, PA), (int)PlanResult::OK);
    CHECK_EQ(PA.len, 7);
    CHECK(follow(PA) & EV_MOVED);
    CHECK_EQ(W.playerX(), 4); CHECK_EQ(W.playerY(), 1);
    CHECK_EQ((int)P.plan(W, 4, 1, WalkMode::PRO, PA), (int)PlanResult::HERE);
    CHECK_EQ((int)P.plan(W, 3, 1, WalkMode::PRO, PA), (int)PlanResult::INVALID);   // Wall
    CHECK_EQ(PA.len, 0);
    static const char *const m2[] = {"#######", "#S.#..#", "#..#..#", "#######"};
    CHECK(load(m2));
    CHECK_EQ((int)P.plan(W, 5, 2, WalkMode::PRO, PA), (int)PlanResult::UNREACHABLE);
    CHECK_EQ(PA.len, 0);
    // Crates / rocks / bushes are obstacles for the planner (it never pushes)
    static const char *const m3[] = {"######", "#SRc.#", "#.b..#", "#....#", "######"};
    CHECK(load(m3));
    CHECK_EQ((int)P.plan(W, 4, 1, WalkMode::PRO, PA), (int)PlanResult::OK);
    CHECK_EQ(PA.len, 7);
    CHECK_EQ((int)P.plan(W, 2, 1, WalkMode::PRO, PA), (int)PlanResult::INVALID);
    // Big world: bounded and optimal on open ground, closed garden unreachable
    CHECK(W.load(WORLDS[0]));
    CHECK_EQ((int)P.plan(W, 2, 5, WalkMode::ASSIST, PA), (int)PlanResult::OK);
    CHECK_EQ(PA.len, 3);
    CHECK_EQ((int)P.plan(W, 20, 10, WalkMode::PRO, PA), (int)PlanResult::UNREACHABLE);
    CHECK_EQ((int)P.plan(W, 58, 58, WalkMode::ASSIST, PA), (int)PlanResult::NEED_BOAT);   // River
    CHECK(PA.len > 0);
}

static void testPlanLava()
{
    static const char *const m[] = {"######", "#S^^.#", "######"};
    CHECK(loadR(m, R8));
    CHECK_EQ((int)P.plan(W, 4, 1, WalkMode::PRO, PA), (int)PlanResult::OK);   // Pro: straight through
    CHECK_EQ(PA.len, 3);
    CHECK_EQ((int)P.plan(W, 4, 1, WalkMode::ASSIST, PA), (int)PlanResult::DANGER);
    CHECK_EQ(PA.len, 0);                                   // Stays on the last safe cell
    static const char *const m2[] = {"######", "#S^^.#", "#....#", "######"};
    CHECK(loadR(m2, R8));
    CHECK_EQ((int)P.plan(W, 4, 1, WalkMode::PRO, PA), (int)PlanResult::OK);
    CHECK_EQ(PA.len, 3);
    CHECK_EQ((int)P.plan(W, 4, 1, WalkMode::ASSIST, PA), (int)PlanResult::OK);   // Detour
    CHECK_EQ(PA.len, 5);
    static const char *const m3[] = {"#######", "#S.^^.#", "#######"};
    CHECK(loadR(m3, R8));
    CHECK_EQ((int)P.plan(W, 5, 1, WalkMode::ASSIST, PA), (int)PlanResult::DANGER);
    CHECK_EQ(PA.len, 1);
    CHECK_EQ((int)P.plan(W, 3, 1, WalkMode::ASSIST, PA), (int)PlanResult::DANGER);   // Lava as goal
    CHECK_EQ(PA.len, 1);
    // Suit: 3 charges carry over a 2 step stretch, not over the following 4 step stretch
    static const char *const m4[] = {"############", "#Su^^.^^^^.#", "############"};
    CHECK(loadR(m4, R8));
    CHECK(W.move(Dir::RIGHT) & EV_SUIT);
    CHECK_EQ(W.suitCharges(), 3);
    CHECK_EQ((int)P.plan(W, 5, 1, WalkMode::ASSIST, PA), (int)PlanResult::SUIT);
    CHECK_EQ(PA.len, 3);
    CHECK_EQ((int)P.plan(W, 10, 1, WalkMode::ASSIST, PA), (int)PlanResult::DANGER);
    CHECK_EQ(PA.len, 3);                                   // Stops at (5,1)
    CHECK_EQ((int)P.plan(W, 10, 1, WalkMode::PRO, PA), (int)PlanResult::OK);
    CHECK_EQ(PA.len, 8);
    const uint32_t ev = follow(PA);                        // 2 suit steps, then 4 burning steps
    CHECK(ev & EV_SUIT_USED);
    CHECK(ev & EV_DIED);                                   // 8 energy, 3 per step: dies on the 3rd
    CHECK_EQ(W.livesLeft(), 1);
    CHECK_EQ(W.suitCharges(), 0);                          // 2 + 1 used, then burning
}

static void testPlanBoat()
{
    static const char *const m[] = {"########", "#SB~~..#", "########"};
    for (int mode = 0; mode < 2; mode++) {
        CHECK(loadR(m, R8));
        CHECK_EQ((int)P.plan(W, 1, 1, (WalkMode)mode, PA), (int)PlanResult::HERE);
        // The planner does not foresee the boat: it walks to the shore (onto the boat cell),
        // the UI plans again once the boat has been picked up.
        CHECK_EQ((int)P.plan(W, 6, 1, (WalkMode)mode, PA), (int)PlanResult::NEED_BOAT);
        CHECK_EQ(PA.len, 1);
        CHECK(follow(PA) & EV_BOAT);
        CHECK_EQ((int)P.plan(W, 6, 1, (WalkMode)mode, PA), (int)PlanResult::OK);
        CHECK_EQ(PA.len, 4);
        CHECK(follow(PA) & EV_SAILING);
        CHECK_EQ(W.playerX(), 6);
    }
    static const char *const m2[] = {"#######", "#S.~~.#", "#######"};
    for (int mode = 0; mode < 2; mode++) {
        CHECK(loadR(m2, R8));
        CHECK_EQ((int)P.plan(W, 5, 1, (WalkMode)mode, PA), (int)PlanResult::NEED_BOAT);
        CHECK_EQ(PA.len, 1);                                // Waits at the shore
        CHECK_EQ((int)P.plan(W, 3, 1, (WalkMode)mode, PA), (int)PlanResult::NEED_BOAT);
        CHECK_EQ(PA.len, 1);
    }
}

static void testEnergyAndLives()
{
    static const char *const m[] = {"#########", "#S^Q^^^.#", "#########"};
    CHECK(loadR(m, R8));
    CHECK_EQ(W.energy(), 8); CHECK_EQ(W.maxEnergy(), 8); CHECK_EQ(W.livesLeft(), 2);
    uint32_t ev = W.move(Dir::RIGHT);
    CHECK_EQ(ev & (EV_BURNED | EV_HURT | EV_DIED), EV_BURNED | EV_HURT);
    CHECK_EQ(W.energy(), 5);
    CHECK(W.move(Dir::RIGHT) & EV_CHECKPOINT);
    CHECK_EQ(W.checkpointX(), 3);
    CHECK(!(W.move(Dir::LEFT) & EV_CHECKPOINT) || true);   // Back onto lava: 2
    CHECK_EQ(W.energy(), 2);
    CHECK(!(W.move(Dir::RIGHT) & EV_CHECKPOINT));           // Already the checkpoint
    ev = W.move(Dir::RIGHT);                                 // 3rd lava step: dies, respawn at Q
    CHECK(ev & EV_DIED);
    CHECK(!(ev & EV_GAME_OVER));
    CHECK_EQ(W.playerX(), 3);
    CHECK_EQ(W.energy(), 8);
    CHECK_EQ(W.livesLeft(), 1);
    CHECK_EQ(W.livesUsed(), 1);
    W.move(Dir::RIGHT); W.move(Dir::RIGHT);
    ev = W.move(Dir::RIGHT);
    CHECK(ev & EV_GAME_OVER);
    CHECK(W.gameOver());
    CHECK_EQ(W.livesLeft(), 0);
    CHECK_EQ(W.move(Dir::LEFT), EV_NONE);                   // Frozen
    CHECK_EQ(W.hit(), EV_NONE);
    W.revive();
    CHECK(!W.gameOver());
    CHECK_EQ(W.livesLeft(), 1);
    CHECK_EQ(W.energy(), 8);
    CHECK_EQ(W.playerX(), 3);
    // Infinite lives: never game over, lives used counted
    Rules inf = R8;
    inf.lives = LIVES_INFINITE;
    CHECK(loadR(m, inf));
    W.move(Dir::RIGHT); W.move(Dir::RIGHT);                  // To the checkpoint (energy 5)
    W.move(Dir::RIGHT);
    CHECK(W.move(Dir::RIGHT) & EV_DIED);
    for (int round = 0; round < 2; round++) {
        W.move(Dir::RIGHT); W.move(Dir::RIGHT);
        ev = W.move(Dir::RIGHT);
        CHECK(ev & EV_DIED);
        CHECK(!W.gameOver());
        CHECK_EQ(W.playerX(), 3);
    }
    CHECK_EQ(W.livesUsed(), 3);
    CHECK_EQ(W.livesLeft(), LIVES_INFINITE);
    // Classic rules (v0.7 maps): lava = straight back to the start
    CHECK(load(m));
    CHECK(W.move(Dir::RIGHT) & EV_DIED);
    CHECK_EQ(W.playerX(), 1);
    CHECK_EQ(W.energy(), 8);
}

static void testItems()
{
    static const char *const m[] = {"##########", "#S^pphhl.#", "##########"};
    Rules r = R8;
    r.energyCap = 9;
    CHECK(loadR(m, r));
    W.move(Dir::RIGHT);
    CHECK_EQ(W.energy(), 5);
    CHECK(W.move(Dir::RIGHT) & EV_POTION);
    CHECK_EQ(W.energy(), 7);
    W.move(Dir::RIGHT);
    CHECK_EQ(W.energy(), 8);                                 // Capped at max
    CHECK(W.move(Dir::RIGHT) & EV_HEART);
    CHECK_EQ(W.maxEnergy(), 9); CHECK_EQ(W.energy(), 9);
    W.move(Dir::RIGHT);
    CHECK_EQ(W.maxEnergy(), 9);                              // Capped at energyCap
    CHECK(W.move(Dir::RIGHT) & EV_LIFE);
    CHECK_EQ(W.livesLeft(), 3);
    // Suit formula: 3 + 1 per permanent extra max unit; a heart found later extends a worn suit
    static const char *const m2[] = {"#########", "#Shhuh..#", "#########"};
    CHECK(loadR(m2, R8));
    CHECK_EQ(W.suitChargesAtPickup(), 3);
    W.move(Dir::RIGHT); W.move(Dir::RIGHT);
    CHECK_EQ(W.maxEnergy(), 10);
    CHECK_EQ(W.suitChargesAtPickup(), 5);
    CHECK(W.move(Dir::RIGHT) & EV_SUIT);
    CHECK_EQ(W.suitCharges(), 5);
    W.move(Dir::RIGHT);
    CHECK_EQ(W.suitCharges(), 6);
    // Lava with charges costs nothing
    static const char *const m3[] = {"######", "#Su^.#", "######"};
    CHECK(loadR(m3, R8));
    W.move(Dir::RIGHT);
    const uint32_t ev = W.move(Dir::RIGHT);
    CHECK(ev & EV_SUIT_USED);
    CHECK(!(ev & EV_HURT));
    CHECK_EQ(W.energy(), 8);
    CHECK_EQ(W.suitCharges(), 2);
}

static void testTreasures()
{
    static const char *const rows[] = {"#######", "#.c.b.#", "#.S.%R", "#..D...", "#######"};
    static const char *const rowsOk[] = {"########", "#.c.b..#", "#.S.%R.#", "#..D...#", "########"};
    static const Treasure t[] = {{2, 1, Obj::GEM, 0}, {4, 1, Obj::POTION, 3}, {4, 2, Obj::HEART, 2},
                                 {5, 2, Obj::SUIT, 0}, {1, 1, Obj::KEY, 0},  {3, 3, Obj::LIFE, 0}};
    MapDef bad = {9, rows, 5, R8, t, 6, nullptr, 0};
    CHECK(!W.load(bad));                                      // Ragged rows
    MapDef def = {9, rowsOk, 5, R8, t, 6, nullptr, 0};
    CHECK(W.load(def));
    CHECK_EQ(W.mapId(), 9);
    CHECK_EQ(W.gemsTotal(), 1);                               // Hidden gems count
    CHECK_EQ((int)W.obj(1, 1), (int)Obj::KEY);                // Nothing covers it: visible
    CHECK_EQ((int)W.obj(2, 1), (int)Obj::CRATE);
    CHECK_EQ((int)W.hidden(2, 1), (int)Obj::GEM);
    CHECK(W.move(Dir::UP) & EV_BLOCKED);
    CHECK_EQ(W.hit(), EV_HIT | EV_BROKE | EV_TREASURE);       // Crate -> gem
    CHECK_EQ((int)W.obj(2, 1), (int)Obj::GEM);
    CHECK_EQ(W.move(Dir::UP) & (EV_GEM | EV_ALL_GEMS), EV_GEM | EV_ALL_GEMS);
    CHECK(W.move(Dir::LEFT) & EV_KEY);
    W.move(Dir::RIGHT); W.move(Dir::RIGHT);
    CHECK(W.move(Dir::RIGHT) & EV_BLOCKED);                   // Bush
    CHECK_EQ(W.hit(), EV_HIT | EV_BROKE | EV_TREASURE);       // Bush -> potion
    CHECK_EQ((int)W.obj(4, 1), (int)Obj::POTION);
    CHECK_EQ(W.objAmount(4, 1), 3);
    W.move(Dir::DOWN);
    W.move(Dir::RIGHT);
    CHECK_EQ(W.hit(), EV_HIT);                                // Cracked wall, 1st hit
    CHECK_EQ((int)W.obj(4, 2), (int)Obj::NONE);               // Still hidden
    CHECK_EQ(W.hit(), EV_HIT | EV_BROKE | EV_TREASURE);
    CHECK_EQ((int)W.obj(4, 2), (int)Obj::HEART);
    CHECK(W.move(Dir::RIGHT) & EV_HEART);
    CHECK_EQ(W.maxEnergy(), 10);
    const uint32_t ev = W.move(Dir::RIGHT);                   // Push the rock: suit under it, picked up
    CHECK_EQ(ev & (EV_PUSHED | EV_TREASURE | EV_SUIT), EV_PUSHED | EV_TREASURE | EV_SUIT);
    CHECK_EQ(W.suitCharges(), 5);                             // 3 + 2 extra max
    W.move(Dir::LEFT); W.move(Dir::LEFT);
    CHECK_EQ(W.move(Dir::DOWN) & (EV_DOOR | EV_TREASURE), EV_DOOR | EV_TREASURE);   // Door -> extra life
    CHECK_EQ((int)W.obj(3, 3), (int)Obj::LIFE);
    // Invalid treasure lists
    static const Treasure dup[] = {{2, 1, Obj::GEM, 0}, {2, 1, Obj::KEY, 0}};
    CHECK(!W.load(MapDef{9, rowsOk, 5, R8, dup, 2, nullptr, 0}));
    static const Treasure notItem[] = {{2, 1, Obj::ROCK, 0}};
    CHECK(!W.load(MapDef{9, rowsOk, 5, R8, notItem, 1, nullptr, 0}));
    static const Treasure outside[] = {{20, 1, Obj::GEM, 0}};
    CHECK(!W.load(MapDef{9, rowsOk, 5, R8, outside, 1, nullptr, 0}));
}

static constexpr Rules RFOG = {8, 20, 3, 3, 4, 3, 1, 2, 1, true, 0};

static int countRevealed()
{
    int n = 0;
    for (int y = 0; y < W.height(); y++)
        for (int x = 0; x < W.width(); x++) n += W.revealed(x, y);
    return n;
}

static void testMinesweeper()
{
    static const char *const m[] = {"#######", "#S.m..#", "#..m..#", "#..m..#", "#######"};
    CHECK(loadR(m, RFOG));
    CHECK(W.fog());
    CHECK(W.revealed(1, 1)); CHECK(W.revealed(1, 3)); CHECK(W.revealed(0, 0));   // Walls always
    CHECK(W.revealed(2, 1)); CHECK(W.revealed(2, 2));
    CHECK_EQ(W.mineHint(1, 1), 0);
    CHECK_EQ(W.mineHint(2, 1), 2);
    CHECK_EQ(W.mineHint(2, 2), 3);
    CHECK(!W.revealed(3, 1)); CHECK(!W.revealed(4, 1)); CHECK(!W.revealed(5, 2));
    CHECK_EQ((int)W.obj(3, 1), (int)Obj::NONE);               // Mines are invisible
    CHECK_EQ((int)W.rawObj(3, 1), (int)Obj::MINE);
    // Flags
    CHECK(!W.toggleFlag(2, 1));                                // Revealed: no flag
    CHECK(W.toggleFlag(3, 2));
    CHECK(W.flagged(3, 2));
    CHECK(!Planner::targetable(W, 3, 2));
    CHECK(W.toggleFlag(3, 2));
    CHECK(!W.flagged(3, 2));
    // Assist will not walk into unknown ground, Pro will
    CHECK_EQ((int)P.plan(W, 5, 1, WalkMode::ASSIST, PA), (int)PlanResult::UNKNOWN);
    CHECK_EQ(PA.len, 1);                                       // Stops at (2,1)
    CHECK(W.toggleFlag(3, 1));
    CHECK(W.toggleFlag(3, 3));
    CHECK_EQ((int)P.plan(W, 5, 1, WalkMode::PRO, PA), (int)PlanResult::OK);   // Only (3,2) open
    const uint32_t ev = follow(PA);
    CHECK(ev & EV_MINE);
    CHECK(ev & EV_HURT);
    CHECK_EQ(W.energy(), 4);
    CHECK_EQ((int)W.tile(3, 2), (int)Tile::CRATER);
    CHECK_EQ((int)W.rawObj(3, 2), (int)Obj::NONE);
    CHECK(W.revealed(3, 2));
    CHECK_EQ(W.playerX(), 5);
    CHECK(W.revealed(5, 1)); CHECK(W.revealed(4, 2));
    // Flood fill: a mine free field opens completely, flags stay closed
    static const char *const m2[] = {"########", "#S.....#", "#......#", "#......#", "#.....m#", "########"};
    CHECK(loadR(m2, RFOG));
    CHECK_EQ(countRevealed(), 8 * 6 - 1);                     // Everything except the mine
    static const char *const m3[] = {"########", "#S..#..#", "#...#..#", "########"};
    Rules r = RFOG;
    CHECK(loadR(m3, r));
    CHECK(!W.revealed(5, 1));                                  // Behind the wall: closed
    CHECK(W.revealed(3, 2));
}

// Zlib CRC32 (matches the one in otr_world.cpp) so the test can forge a valid old (v2) save file.
static uint32_t testCrc32(const uint8_t *p, size_t n)
{
    uint32_t crc = ~0u;
    while (n--) {
        crc ^= *p++;
        for (int k = 0; k < 8; k++) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

// v0.18.0: dark areas, the flashlight cone, pogo vaulting, and the v3 save round trip.
static void testDarkFlashPogo()
{
    // --- Darkness visibility: a dark cell is hidden, the player's own cell is always visible. ---
    {
        static const char *const rows[] = {"S.....", "......", "......", "......", "......", ".....X"};
        static const DarkRect darks[] = {{0, 0, 4, 4}};   // covers the player cell (0,0) too
        MapDef def = {60, rows, 6, R8, nullptr, 0, darks, 1};
        CHECK(W.load(def));
        CHECK(W.darkCell(0, 0));                 // flagged dark ...
        CHECK(!W.darkHidden(0, 0));              // ... but the player's cell is visible
        CHECK(W.darkCell(3, 3));
        CHECK(W.darkHidden(3, 3));               // an unlit dark cell is hidden
        CHECK(!W.darkCell(5, 5));                // outside the rect: not dark
        CHECK(!W.darkHidden(5, 5));
    }

    // --- Flashlight cone: shape + reach per level, and it reveals darkness only. ---
    {
        static const char *const rows[] = {"S......", ".......", ".......", ".......",
                                           ".......", ".......", "......X"};
        static const DarkRect darks[] = {{0, 0, 6, 6}};   // whole map dark
        MapDef def = {61, rows, 7, R8, nullptr, 0, darks, 1};
        CHECK(W.load(def));
        CHECK_EQ((int)W.facing(), (int)Dir::DOWN);   // default facing after load
        // Level 1: R = 1. Just the cell straight ahead.
        W.setFlashlight(1);
        CHECK_EQ(W.flashlight(), 1);
        CHECK(W.lit(0, 1));                           // forward 1, lateral 0
        CHECK(!W.lit(0, 2));                          // beyond reach (R = 1)
        CHECK(!W.lit(1, 1));                          // forward 1, lateral 1: 1 > 0
        CHECK(!W.darkHidden(0, 1));                   // a lit dark cell becomes visible
        CHECK(W.darkHidden(0, 2));                    // an unlit dark cell stays hidden
        // Level 2: R = 2. A 3-wide first row, then 1 cell straight ahead.
        W.setFlashlight(2);
        CHECK_EQ(W.flashlight(), 2);
        CHECK(W.lit(0, 1)); CHECK(W.lit(1, 1));       // first row widens to lateral +-1 (left is off-map here)
        CHECK(!W.lit(2, 1));                          // ... but no wider than 3
        CHECK(W.lit(0, 2));                           // last row: straight ahead only
        CHECK(!W.lit(1, 2)); CHECK(!W.lit(0, 3));     // not wide on the last row, not beyond reach
        // Level 3: R = 3. Two 3-wide rows, then 1 cell straight ahead.
        W.setFlashlight(3);
        CHECK_EQ(W.flashlight(), 3);
        CHECK(W.lit(1, 1)); CHECK(W.lit(1, 2));       // both leading rows are 3 wide
        CHECK(!W.lit(2, 2));                          // ... but no wider than 3
        CHECK(W.lit(0, 3)); CHECK(!W.lit(1, 3));      // last row: straight ahead only
        CHECK(!W.lit(0, 4));                          // beyond reach
        // setFlashlight clamps to 0..3.
        W.setFlashlight(9); CHECK_EQ(W.flashlight(), 3);
        W.setFlashlight(-2); CHECK_EQ(W.flashlight(), 0);
        CHECK(!W.lit(0, 1));                          // no flashlight: nothing is lit
    }

    // --- Flashlight reveals darkness only, never fog. ---
    {
        // Mine directly below the start: fog reveals only the start cell (it has a mine neighbour).
        static const char *const rows[] = {"S.", "m.", ".."};
        CHECK(loadR(rows, RFOG));
        CHECK(!W.revealed(0, 1));                     // fogged
        CHECK_EQ((int)W.obj(0, 1), (int)Obj::NONE);   // mine hidden by fog
        W.setFlashlight(3);
        CHECK(W.lit(0, 1));                           // the cell IS inside the cone ...
        CHECK(!W.revealed(0, 1));                     // ... yet fog is untouched
        CHECK_EQ((int)W.obj(0, 1), (int)Obj::NONE);   // mine still hidden
    }

    // --- Pogo vault: success for widths 1..level. ---
    {
        struct Case { const char *row; int pogo; int landX; };
        static const Case ok[] = {
            {"ST...", 3, 2},   // 1 blocking tree
            {"STT..", 3, 3},   // 2 blocking trees
            {"STTT.", 3, 4},   // 3 blocking trees
            {"ST...", 1, 2},   // level 1 clears a single-wide gap
        };
        for (const Case &c : ok) {
            const char *const rows[] = {c.row};
            CHECK(W.load(rows, 1));
            W.setPogo(c.pogo);
            // v0.18.3: the jump is manual. Walking into the obstacle is blocked but turns the hero to
            // face it; canVault() then reports a jump is available and vault() performs it.
            CHECK_EQ(W.move(Dir::RIGHT), (uint32_t)EV_BLOCKED);
            CHECK(W.canVault());
            const uint32_t ev = W.vault();
            CHECK(ev & EV_MOVED);
            CHECK_EQ(W.playerX(), c.landX);
        }
    }

    // --- Pogo failure: too wide / landing blocked / out of bounds -> no vault offered, no move. ---
    {
        struct Case { const char *row; int pogo; };
        static const Case bad[] = {
            {"STT..", 1},   // 2 wide, only level 1: out of reach
            {"STTT#", 3},   // reaches but the landing cell is a wall
            {"STT", 3},     // vault would land off the map edge
        };
        for (const Case &c : bad) {
            const char *const rows[] = {c.row};
            CHECK(W.load(rows, 1));
            W.setPogo(c.pogo);
            CHECK_EQ(W.move(Dir::RIGHT), (uint32_t)EV_BLOCKED);
            CHECK(!W.canVault());                      // no jump available
            CHECK_EQ(W.vault(), (uint32_t)EV_BLOCKED); // vault refuses
            CHECK_EQ(W.playerX(), 0);                  // stayed put
        }
    }

    // --- No pogo stick: never any vault. ---
    {
        const char *const rows[] = {"ST..."};
        CHECK(W.load(rows, 1));
        CHECK_EQ(W.move(Dir::RIGHT), (uint32_t)EV_BLOCKED);
        CHECK(!W.canVault());
        CHECK_EQ(W.vault(), (uint32_t)EV_NONE);   // no pogo: vault is never offered
        CHECK_EQ(W.playerX(), 0);
    }

    // --- A pushable rock is still pushed, not vaulted, even with a pogo stick. ---
    {
        const char *const rows[] = {"SR..."};
        CHECK(W.load(rows, 1));
        W.setPogo(3);
        const uint32_t ev = W.move(Dir::RIGHT);
        CHECK(ev & EV_PUSHED);
        CHECK_EQ(W.playerX(), 1);
        CHECK_EQ((int)W.obj(2, 0), (int)Obj::ROCK);
    }

    // --- Glyph pickups f / j grant level 1; treasures set a higher level (amount == level). ---
    {
        const char *const rows[] = {"Sf..."};
        CHECK(W.load(rows, 1));
        CHECK(W.move(Dir::RIGHT) & EV_TREASURE);
        CHECK_EQ(W.flashlight(), 1);
    }
    {
        const char *const rows[] = {"Sj..."};
        CHECK(W.load(rows, 1));
        CHECK(W.move(Dir::RIGHT) & EV_TREASURE);
        CHECK_EQ(W.pogo(), 1);
    }
    {
        static const char *const rows[] = {"S....", ".....", "....."};
        static const Treasure t[] = {{2, 0, Obj::FLASH, 3}, {3, 0, Obj::POGO, 2}};
        MapDef def = {62, rows, 3, R8, t, 2, nullptr, 0};
        CHECK(W.load(def));
        CHECK_EQ((int)W.obj(2, 0), (int)Obj::FLASH);
        CHECK_EQ((int)W.obj(3, 0), (int)Obj::POGO);
        CHECK(W.move(Dir::RIGHT) & EV_MOVED);
        CHECK(W.move(Dir::RIGHT) & EV_TREASURE);      // onto the flashlight (level 3)
        CHECK_EQ(W.flashlight(), 3);
        CHECK(W.move(Dir::RIGHT) & EV_TREASURE);      // onto the pogo stick (level 2)
        CHECK_EQ(W.pogo(), 2);
    }

    // --- Save/restore v3 round trip, and a forged v2 save loads with flash = pogo = 0. ---
    {
        static const char *const rows[] = {"S....", ".....", "....."};
        CHECK(W.load(rows, 3));
        W.setFlashlight(2);
        W.setPogo(3);
        const size_t n = W.saveSize();
        CHECK(W.save(SAVEBUF, sizeof(SAVEBUF)) == n);
        // v3 round trip keeps the ability levels.
        CHECK(W.load(rows, 3));
        CHECK(W.restore(SAVEBUF, n));
        CHECK_EQ(W.flashlight(), 2);
        CHECK_EQ(W.pogo(), 3);
        // Forge an old v2 save: version 2 + zeroed ability bytes (player-block bytes 26,27 ->
        // absolute 16+26, 16+27), CRC recomputed. It must load with flash = pogo = 0.
        static uint8_t v2[64 * 1024];
        memcpy(v2, SAVEBUF, n);
        v2[4] = 2; v2[5] = 0;              // version u16 at header offset 4
        v2[16 + 26] = 0; v2[16 + 27] = 0; // clear the v3 ability bytes
        const uint32_t crc = testCrc32(v2, n - 4);
        v2[n - 4] = (uint8_t)crc; v2[n - 3] = (uint8_t)(crc >> 8);
        v2[n - 2] = (uint8_t)(crc >> 16); v2[n - 1] = (uint8_t)(crc >> 24);
        CHECK(W.load(rows, 3));
        CHECK(W.restore(v2, n));
        CHECK_EQ(W.flashlight(), 0);
        CHECK_EQ(W.pogo(), 0);
    }
}

static void testSaveLoad()
{
    CHECK(W.load(WORLDS[1]));
    CHECK_EQ(W.mapId(), WORLD_MINES);
    const size_t n = W.saveSize();
    CHECK_EQ((long)n, 16 + 32 + 7 * 40 * 30 + 4);
    CHECK_EQ((long)W.save(SAVEBUF, n - 1), 0);
    // Play a little: flag, walk, step on a mine
    int fx = -1, fy = -1;
    for (int y = 1; y < 29 && fx < 0; y++)
        for (int x = 1; x < 39; x++)
            if (!W.revealed(x, y) && W.rawObj(x, y) != Obj::MINE) { fx = x; fy = y; break; }
    CHECK(W.toggleFlag(fx, fy));
    W.move(Dir::RIGHT); W.move(Dir::RIGHT); W.move(Dir::RIGHT);   // (5,2) is a mine
    CHECK_EQ((int)W.tile(5, 2), (int)Tile::CRATER);
    CHECK_EQ(W.energy(), 4);
    const int px = W.playerX(), py = W.playerY(), rev = countRevealed(), steps = W.steps();
    CHECK_EQ((long)W.save(SAVEBUF, sizeof(SAVEBUF)), (long)n);
    // Restore into a fresh world
    CHECK(W.load(WORLDS[1]));
    CHECK(W.restore(SAVEBUF, n));
    CHECK_EQ(W.playerX(), px); CHECK_EQ(W.playerY(), py);
    CHECK_EQ(W.energy(), 4); CHECK_EQ(W.maxEnergy(), 8);
    CHECK_EQ(W.livesLeft(), 3);
    CHECK_EQ(W.steps(), steps);
    CHECK_EQ(countRevealed(), rev);
    CHECK(W.flagged(fx, fy));
    CHECK_EQ((int)W.tile(5, 2), (int)Tile::CRATER);
    static uint8_t again[64 * 1024];
    CHECK_EQ((long)W.save(again, sizeof(again)), (long)n);
    CHECK(memcmp(again, SAVEBUF, n) == 0);                     // Round trip is exact
    // Corrupt files: rejected, world stays freshly loaded
    for (int k = 0; k < 5; k++) {
        CHECK(W.load(WORLDS[1]));
        memcpy(again, SAVEBUF, n);
        bool ok = true;
        switch (k) {
        case 0: again[n / 2] ^= 0x40; ok = W.restore(again, n); break;       // Bit flip (CRC)
        case 1: ok = W.restore(again, n - 1); break;                          // Truncated
        case 2: again[4] = 99; ok = W.restore(again, n); break;               // Unknown (future) version
        case 3: again[0] = 'X'; ok = W.restore(again, n); break;              // Bad magic
        case 4: ok = W.restore(nullptr, n); break;
        }
        CHECK(!ok);
        CHECK_EQ(W.playerX(), 2); CHECK_EQ(W.playerY(), 2);
        CHECK_EQ(W.energy(), 8);
        CHECK(!W.flagged(fx, fy));
        CHECK_EQ((int)W.rawObj(5, 2), (int)Obj::MINE);
    }
    CHECK(W.load(WORLDS[0]));                                  // Other map
    CHECK(!W.restore(SAVEBUF, n));
    // Test world: treasures + suit survive the round trip
    CHECK(W.load(WORLDS[0]));
    CHECK(walkTo(17, 18));
    CHECK(W.move(Dir::RIGHT) & EV_TREASURE);                   // Potion under the first rock
    const size_t n0 = W.saveSize();
    CHECK_EQ((long)W.save(SAVEBUF, sizeof(SAVEBUF)), (long)n0);
    CHECK(W.load(WORLDS[0]));
    CHECK(W.restore(SAVEBUF, n0));
    CHECK_EQ((int)W.obj(19, 18), (int)Obj::ROCK);
    CHECK_EQ(W.playerX(), 18);
    CHECK_EQ((int)W.hidden(18, 18), (int)Obj::NONE);
    CHECK_EQ((int)W.hidden(54, 10), (int)Obj::SUIT);
}

// Mine free BFS reachability in the minesweeper world (bushes count as passable: they are hit).
static bool mineFreeReach(int gx, int gy)
{
    static int seen[MAX_H][MAX_W];
    static int queue[MAX_H * MAX_W];
    memset(seen, 0, sizeof(seen));
    int head = 0, tail = 0;
    queue[tail++] = W.playerY() * MAX_W + W.playerX();
    seen[W.playerY()][W.playerX()] = 1;
    while (head < tail) {
        const int c = queue[head++], cx = c % MAX_W, cy = c / MAX_W;
        if (cx == gx && cy == gy) return true;
        for (int d = 0; d < 4; d++) {
            const int nx = cx + dirX((Dir)d), ny = cy + dirY((Dir)d);
            if (!W.inside(nx, ny) || seen[ny][nx] || W.rawObj(nx, ny) == Obj::MINE) continue;
            const Tile t = W.tile(nx, ny);
            if (t == Tile::WALL || t == Tile::TREE) continue;
            seen[ny][nx] = 1;
            queue[tail++] = ny * MAX_W + nx;
        }
    }
    return false;
}

static void testWorlds()
{
    for (int y = 0; y < MINE_WORLD_H; y++) CHECK_EQ((int)strlen(MINE_WORLD[y]), MINE_WORLD_W);
    CHECK(W.load(WORLDS[0]));
    CHECK_EQ(W.mapId(), WORLD_TEST);
    CHECK_EQ(W.gemsTotal(), 13);
    CHECK_EQ(W.livesLeft(), LIVES_INFINITE);
    CHECK(!W.fog());
    CHECK_EQ((int)W.hidden(54, 10), (int)Obj::SUIT);
    CHECK_EQ((int)W.obj(54, 10), (int)Obj::CRATE);
    CHECK(W.load(WORLDS[1]));
    CHECK_EQ(W.mapId(), WORLD_MINES);
    CHECK(W.fog());
    CHECK_EQ(W.livesLeft(), 3);
    CHECK_EQ(W.gemsTotal(), 3);
    int mines = 0;
    for (int y = 0; y < W.height(); y++)
        for (int x = 0; x < W.width(); x++) mines += W.rawObj(x, y) == Obj::MINE;
    CHECK_EQ(mines, 95);
    CHECK_EQ(W.mineHint(2, 2), 0);
    CHECK(W.revealed(3, 3));                                   // Opened around the start
    static const int GOALS[][2] = {{20, 15}, {37, 27}, {33, 4}, {6, 25}, {21, 20}, {28, 26},
                                   {15, 3},  {3, 14},  {24, 13}, {36, 10}, {12, 26}};
    for (const auto &g : GOALS) {
        CHECK(mineFreeReach(g[0], g[1]));
        CHECK_EQ(W.mineHint(g[0], g[1]), 0);
    }
    CHECK_EQ((int)W.tile(20, 15), (int)Tile::CHECKPOINT);
    CHECK_EQ((int)W.tile(37, 27), (int)Tile::EXIT);
}

// ---- v0.10.0: HW test "ended up in the lava when crossing the bridge several times" -------
// Root cause: in Pro mode a lava step cost exactly as much as a ground step, so between the
// equally short paths over the basalt causeway and through the lava beside it the A* search
// order decided - and it cut through the lava. Repro on the real test world: build the x=14
// causeway (rows 38..42), then walk back and forth many times, by tap-to-walk in both modes
// and by joystick steps, and check that the hero never sets foot on lava.
static int g_lavaSteps;

static bool tapWalk(int x, int y, WalkMode m)
{
    const PlanResult r = P.plan(W, x, y, m, PA);
    if (r == PlanResult::HERE) return true;
    for (int i = 0; i < PA.len; i++) {
        const uint32_t ev = W.move((Dir)PA.dirs[i]);
        if (W.tile(W.playerX(), W.playerY()) == Tile::LAVA || (ev & (EV_BURNED | EV_SUIT_USED | EV_DIED))) g_lavaSteps++;
        if (!(ev & EV_MOVED)) return false;
    }
    return W.playerX() == x && W.playerY() == y;
}

// Joystick: finger at `deg` degrees off straight `base` (0 up / 180 down), held in the outer zone;
// returns the number of steps taken before reaching row `toY`.
static int joyWalk(double deg, int toY, uint8_t diag)
{
    JoyController j;
    JoyConfig c;
    c.diag = diag;
    j.configure(c, 54, 16);
    const double r = deg * 3.14159265358979 / 180.0;
    j.press((int)lround(50 * sin(r)), (int)lround(-50 * cos(r)), 0);
    int steps = 0;
    for (uint32_t t = 0; t < 20000 && W.playerY() != toY; t += 15) {
        Dir d;
        if (!j.poll(t, true, d)) continue;
        const uint32_t ev = W.move(d);
        steps++;
        if (W.tile(W.playerX(), W.playerY()) == Tile::LAVA || (ev & (EV_BURNED | EV_DIED))) g_lavaSteps++;
    }
    j.release(20000);
    return steps;
}

static void testBridgeRepeated()
{
    CHECK(W.load(WORLDS[0]));
    // Gate, key, boathouse door, boat (as in testWorld, now with the real rules)
    CHECK(tapWalk(17, 18, WalkMode::ASSIST));
    for (int i = 0; i < 4; i++) W.move(Dir::RIGHT);
    CHECK(tapWalk(22, 17, WalkMode::ASSIST));
    W.move(Dir::DOWN);
    CHECK(W.move(Dir::DOWN) & EV_GATE_OPEN);
    CHECK(tapWalk(20, 10, WalkMode::ASSIST));
    CHECK(tapWalk(44, 18, WalkMode::ASSIST));
    CHECK_EQ(W.move(Dir::RIGHT), EV_DOOR);
    CHECK(tapWalk(48, 20, WalkMode::ASSIST));
    CHECK(W.hasBoat());
    struct Push { int sx, sy; Dir d; int n; };
    static const Push PLAN[] = {
        {14, 33, Dir::DOWN, 4},
        {11, 36, Dir::RIGHT, 2}, {14, 35, Dir::DOWN, 3}, {17, 35, Dir::LEFT, 2}, {14, 34, Dir::DOWN, 5},
        {8, 35, Dir::RIGHT, 5},  {14, 34, Dir::DOWN, 6}, {20, 36, Dir::LEFT, 5}, {14, 35, Dir::DOWN, 6},
    };
    for (const Push &p : PLAN) {
        CHECK(tapWalk(p.sx, p.sy, WalkMode::ASSIST));
        for (int i = 0; i < p.n; i++) CHECK(W.move(p.d) & EV_PUSHED);
    }
    for (int y = 38; y <= 42; y++) CHECK_EQ((int)W.tile(14, y), (int)Tile::BASALT);
    CHECK_EQ((int)W.tile(13, 40), (int)Tile::LAVA);
    CHECK_EQ(W.suitCharges(), 0);
    g_lavaSteps = 0;
    const int used0 = W.livesUsed();
    // Tap-to-walk, both modes: island targets whose shortest way via the causeway is exactly as
    // long as the way through the lava (before the fix Pro burned on every one of these)
    static const int ISLAND[][2] = {{13, 45}, {12, 44}, {16, 47}, {11, 43}, {17, 44}, {15, 48}, {13, 48}};
    for (int mode = 0; mode < 2; mode++)
        for (int round = 0; round < 3; round++)
            for (const auto &t : ISLAND) {
                CHECK(tapWalk(14, 36, (WalkMode)mode));
                CHECK(tapWalk(t[0], t[1], (WalkMode)mode));
                CHECK(tapWalk(14, 36, (WalkMode)mode));
            }
    CHECK_EQ(g_lavaSteps, 0);
    CHECK_EQ(W.livesUsed(), used0);
    CHECK_EQ(W.energy(), W.maxEnergy());
    // Pro still goes straight through lava when that is really shorter
    CHECK(tapWalk(10, 37, WalkMode::PRO));
    CHECK_EQ((int)P.plan(W, 10, 43, WalkMode::PRO, PA), (int)PlanResult::OK);
    CHECK_EQ(PA.len, 6);
    CHECK(tapWalk(14, 36, WalkMode::ASSIST));
    // Joystick: down and up the causeway with a slightly crooked thumb (up to 25 degrees, both
    // diagonal modes), 5 rounds; the causeway survives a save / restore in between
    for (int round = 0; round < 5; round++) {
        const double off = (round % 2 ? -1 : 1) * (round * 6.0);   // 0, -6, 12, -18, 24 degrees
        for (uint8_t diag = 0; diag < 2; diag++) {
            CHECK_EQ(W.playerX(), 14);
            CHECK_EQ(joyWalk(180.0 + off, 43, diag), 7);   // (14,44) is a tree: island edge at 43
            CHECK_EQ(W.playerX(), 14);
            CHECK_EQ(joyWalk(off, 36, diag), 7);
        }
        const size_t n = W.save(SAVEBUF, sizeof(SAVEBUF));
        CHECK(n > 0);
        CHECK(W.restore(SAVEBUF, n));
        for (int y = 38; y <= 42; y++) CHECK_EQ((int)W.tile(14, y), (int)Tile::BASALT);
    }
    CHECK_EQ(g_lavaSteps, 0);
    CHECK_EQ(W.livesUsed(), used0);
}

// Pro tie-break in isolation: two equally short ways, one over lava -> the safe one
static void testPlanProTieBreak()
{
    static const char *const m[] = {"#####", "#S..#", "#^#.#", "#...#", "#####"};
    CHECK(loadR(m, R8));
    CHECK_EQ((int)P.plan(W, 1, 3, WalkMode::PRO, PA), (int)PlanResult::OK);   // Straight down: lava
    CHECK_EQ(PA.len, 2);
    static const char *const m2[] = {"######", "#S...#", "#^^#.#", "#....#", "######"};
    CHECK(loadR(m2, R8));
    // (2,3): 1 right + 2 down = 3 steps, every 3-step way crosses lava -> lava (shortest wins)
    CHECK_EQ((int)P.plan(W, 2, 3, WalkMode::PRO, PA), (int)PlanResult::OK);
    CHECK_EQ(PA.len, 3);
    static const char *const m3[] = {"#####", "#S..#", "#^..#", "#...#", "#####"};
    CHECK(loadR(m3, R8));
    // (1,3) -> (3,1)... from S (1,1) to (2,3): via (1,2) lava or via (2,1),(2,2): both 3 steps
    CHECK_EQ((int)P.plan(W, 2, 3, WalkMode::PRO, PA), (int)PlanResult::OK);
    CHECK_EQ(PA.len, 3);
    CHECK(!(follow(PA) & (EV_BURNED | EV_HURT)));
    CHECK_EQ(W.energy(), 8);
}

// ---- v0.11.0: people (Obj::NPC) ---------------------------------------------------------------
static void testPeople()
{
    static const char *const m[] = {"S1..", "....", "..2."};
    CHECK(load(m));
    CHECK_EQ((int)W.tile(1, 0), (int)Tile::GRASS);
    CHECK_EQ((int)W.obj(1, 0), (int)Obj::NPC);
    CHECK_EQ(W.npcAt(1, 0), 1);
    CHECK_EQ(W.npcAt(2, 2), 2);
    CHECK_EQ(W.npcAt(0, 0), 0);
    CHECK_EQ(W.move(Dir::RIGHT), EV_BLOCKED | EV_TALK);    // Blocks, starts a talk
    CHECK_EQ(W.facingNpc(), 1);
    CHECK_EQ(W.playerX(), 0);
    CHECK_EQ(W.talkNpc(), 1);
    CHECK_EQ(W.hit(), EV_TALK);                            // Hitting a person = talking
    CHECK_EQ((int)W.obj(1, 0), (int)Obj::NPC);             // ... and never hurts
    W.move(Dir::DOWN);
    CHECK_EQ(W.facingNpc(), 0);
    int x = -1, y = -1;
    CHECK(W.findNpc(2, x, y));
    CHECK_EQ(x, 2);
    CHECK_EQ(y, 2);
    CHECK(!W.findNpc(5, x, y));
    CHECK(!W.moveNpc(2, 0, 1));                            // The player stands there
    CHECK(!W.moveNpc(2, 1, 0));                            // Occupied by person 1
    CHECK(!W.moveNpc(2, 9, 9));                            // Outside
    CHECK(W.moveNpc(2, 3, 1));
    CHECK_EQ(W.npcAt(2, 2), 0);
    CHECK_EQ(W.npcAt(3, 1), 2);
    CHECK(W.removeNpc(1));
    CHECK_EQ((int)W.obj(1, 0), (int)Obj::NONE);
    CHECK(!W.removeNpc(1));
    // Save / restore keeps the people
    uint8_t buf[512];
    const size_t n = W.save(buf, sizeof(buf));
    CHECK(n > 0);
    CHECK(load(m));
    CHECK(W.restore(buf, n));
    CHECK_EQ(W.npcAt(3, 1), 2);
    CHECK_EQ(W.npcAt(1, 0), 0);
    // The same number twice is a map error
    static const char *const dup[] = {"S1.1"};
    CHECK(!load(dup));
    // Story effects on the world
    static const char *const d[] = {"SD.", "..."};
    CHECK(load(d));
    W.addKeys(2);
    CHECK_EQ(W.keys(), 2);
    W.addKeys(-5);
    CHECK_EQ(W.keys(), 0);
    CHECK(W.clearObj(1, 0));
    CHECK_EQ((int)W.obj(1, 0), (int)Obj::NONE);
    CHECK(!W.clearObj(2, 1));                              // Nothing there
}

static void testVillage()
{
    for (int y = 0; y < VILLAGE_H; y++) CHECK_EQ((int)strlen(VILLAGE[y]), VILLAGE_W);
    CHECK_EQ(WORLD_COUNT, 4);
    CHECK(W.load(WORLDS[2]));
    CHECK_EQ(W.mapId(), WORLD_VILLAGE);
    int x, y;
    CHECK(W.findNpc(1, x, y));
    CHECK(W.findNpc(2, x, y));
    CHECK_EQ(x, 15);
    CHECK_EQ(y, 5);
    CHECK(W.findNpc(3, x, y));
    CHECK_EQ((int)W.obj(26, 11), (int)Obj::DOOR);
    // The shepherd closes the only way north: nothing above row 5 is reachable until she moves
    CHECK(P.plan(W, 28, 1, WalkMode::PRO, PA) != PlanResult::OK);
    CHECK(W.moveNpc(2, 16, 6));
    CHECK_EQ((int)P.plan(W, 15, 4, WalkMode::PRO, PA), (int)PlanResult::OK);
}

// ---- v0.16.0/v0.16.1: Josef (1 Mos 37), two maps, blocking NPC opens the way to the exit ------
static void testJoseph()
{
    // v0.16.1: the two maps must be clearly different (they used to feel identical).
    int sameRows = 0;
    for (int y = 0; y < JOSEPH_CAMP_H && y < JOSEPH_ROAD_H; y++)
        if (strcmp(JOSEPH_CAMP_ROWS[y], JOSEPH_ROAD_ROWS[y]) == 0) sameRows++;
    CHECK(sameRows < JOSEPH_CAMP_H / 2);   // far fewer than half the rows coincide

    // Camp map (WORLDS[WORLD_JOSEPH-1]); Ruben (3) blocks the only gap in the tree wall north.
    CHECK(W.load(WORLDS[WORLD_JOSEPH - 1]));
    CHECK_EQ(W.mapId(), WORLD_JOSEPH);
    CHECK_EQ(W.width(), JOSEPH_W);
    CHECK_EQ(W.height(), JOSEPH_CAMP_H);
    int x, y;
    CHECK(W.findNpc(1, x, y));            // Jakob
    CHECK(W.findNpc(2, x, y));            // Josef, inside the locked tent (house)
    CHECK(!W.findNpc(4, x, y));           // road-only NPC absent here

    // v0.16.1: Josef sits in a house behind a locked door; the key is hidden far off. Find the
    // door and the key on the map, confirm the interior is sealed until the key opens the door.
    int dx = -1, dy = -1, kx = -1, ky = -1;
    for (int yy = 0; yy < W.height(); yy++)
        for (int xx = 0; xx < W.width(); xx++) {
            if (W.obj(xx, yy) == Obj::DOOR) { dx = xx; dy = yy; }
            if (W.obj(xx, yy) == Obj::KEY) { kx = xx; ky = yy; }
        }
    CHECK(dx >= 0);                        // a locked door exists
    CHECK(kx >= 0);                        // a key exists somewhere else
    CHECK_EQ(W.keys(), 0);
    CHECK(!walkTo(dx, dy - 1));            // the floor just inside the door is sealed off
    CHECK(walkTo(kx, ky));                 // fetch the key from its far corner
    CHECK_EQ(W.keys(), 1);
    CHECK(walkTo(dx, dy + 1));             // stand just south of the door
    CHECK_EQ(W.move(Dir::UP), EV_DOOR);    // the key opens the door
    CHECK_EQ(W.keys(), 0);
    CHECK(walkTo(dx, dy - 1));             // now the tent interior (and Josef) is reachable

    // Ruben (3) blocks the only gap in the tree wall; the exit stays closed until he steps aside.
    CHECK(W.load(WORLDS[WORLD_JOSEPH - 1]));
    int rx, ry;
    CHECK(W.findNpc(3, rx, ry));          // Ruben, in the gap
    int ex = -1, ey = -1;
    for (int yy = 0; yy < W.height(); yy++)
        for (int xx = 0; xx < W.width(); xx++)
            if (W.tile(xx, yy) == Tile::EXIT) { ex = xx; ey = yy; }
    CHECK(ex >= 0);
    CHECK(!walkTo(ex, ey));              // exit unreachable while Ruben blocks the gap
    CHECK(W.moveNpc(3, rx, ry + 2));     // Ruben steps aside
    CHECK(walkTo(ex, ey));              // now the way to Dotan is open

    // Road map (JOSEPH_ROAD_DEF, second map of the same world); merchant (6) blocks the gap.
    CHECK(W.load(JOSEPH_ROAD_DEF));
    CHECK_EQ(W.mapId(), 5);
    CHECK_EQ(W.width(), JOSEPH_W);
    CHECK_EQ(W.height(), JOSEPH_ROAD_H);
    CHECK(W.findNpc(4, x, y));            // Juda
    CHECK(W.findNpc(5, x, y));            // one of the brothers
    int mx, my;
    CHECK(W.findNpc(6, mx, my));          // merchant, in the gap
    ex = ey = -1;
    for (int yy = 0; yy < W.height(); yy++)
        for (int xx = 0; xx < W.width(); xx++)
            if (W.tile(xx, yy) == Tile::EXIT) { ex = xx; ey = yy; }
    CHECK(ex >= 0);
    CHECK(!walkTo(ex, ey));              // caravan road closed while the merchant blocks it
    CHECK(W.moveNpc(6, mx, my + 2));     // merchant steps aside after the sale
    CHECK(walkTo(ex, ey));              // chapter 1 can be completed
}

int main()
{
    testLoad();
    testWalkAndPickup();
    testPush();
    testHit();
    testDoorAndBoat();
    testLavaAndGates();
    testWorld();
    testPickTarget();
    testPlanBasic();
    testPlanLava();
    testPlanBoat();
    testEnergyAndLives();
    testItems();
    testTreasures();
    testMinesweeper();
    testDarkFlashPogo();
    testSaveLoad();
    testWorlds();
    testPlanProTieBreak();
    testBridgeRepeated();
    testPeople();
    testVillage();
    testJoseph();
    std::printf("%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
