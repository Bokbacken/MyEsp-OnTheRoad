// Host unit tests for the OnTheRoad Engine tile world (components/otr_engine):
//   g++ -std=c++17 -Wall -Wextra -Icomponents/otr_engine/include tests/test_adventure.cpp components/otr_engine/otr_world.cpp components/otr_engine/otr_maps.cpp -o tests/test_adventure && ./tests/test_adventure
#include <cstdio>
#include <cstring>

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

int main()
{
    testLoad();
    testWalkAndPickup();
    testPush();
    testHit();
    testDoorAndBoat();
    testLavaAndGates();
    testWorld();
    std::printf("%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
