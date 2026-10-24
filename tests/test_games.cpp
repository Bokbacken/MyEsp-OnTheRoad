// Host unit tests for Pig, Farkle and Ship/Captain/Crew:
//   g++ -std=c++17 -Wall -Wextra -Imain tests/test_games.cpp main/game_pig.cpp main/game_farkle.cpp main/game_scc.cpp -o tests/test_games && ./tests/test_games
#include <cstdio>
#include <cstring>
#include <initializer_list>

#include "game_farkle.h"
#include "game_pig.h"
#include "game_scc.h"

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

// Scripted RNG: returns the queued values in order (wraps around).
struct Script {
    uint8_t v[64];
    int n = 0, pos = 0;
    void set(std::initializer_list<int> d)
    {
        n = 0;
        pos = 0;
        for (int x : d) v[n++] = (uint8_t)x;
    }
};
static uint8_t scripted(void *ctx)
{
    Script *s = (Script *)ctx;
    uint8_t r = s->v[s->pos % s->n];
    s->pos++;
    return r;
}

// Zero-initialised like the globals on the device (engines have no constructors).
static PigGame g_pig;
static FarkleGame g_fk;
static SccGame g_scc;

static void testPigOneDie()
{
    Script s;
    PigGame &g = g_pig;
    g.reset(PigVariant::ONE_DIE);
    g.clearPlayers();
    g.addPlayer("A");
    g.addPlayer("B");
    g.setRng(scripted, &s);
    g.start();
    CHECK_EQ(g.canBank(), 0);
    s.set({4, 6, 1});
    CHECK_EQ(g.roll(), 1);
    CHECK_EQ(g.turnTotal(), 4);
    g.roll();
    CHECK_EQ(g.turnTotal(), 10);
    CHECK_EQ(g.bank(), 1);
    CHECK_EQ(g.total(0), 10);
    CHECK_EQ(g.currentPlayer(), 1);
    g.roll();   // 1 -> bust
    CHECK_EQ(g.hasBusted(), 1);
    CHECK_EQ(g.turnTotal(), 0);
    CHECK_EQ(g.canRoll(), 0);
    CHECK_EQ(g.canBank(), 0);
    CHECK_EQ(g.passTurn(), 1);
    CHECK_EQ(g.currentPlayer(), 0);
    CHECK_EQ(g.round(), 2);
    // Win: roll 6s until >= 90 more
    s.set({6});
    for (int i = 0; i < 15; i++) g.roll();
    CHECK_EQ(g.turnTotal(), 90);
    g.bank();
    CHECK_EQ(g.finished(), 1);
    CHECK_EQ(g.winner(), 0);
    int order[8], places[8];
    g.ranking(order, places);
    CHECK_EQ(order[0], 0);
    CHECK_EQ(places[1], 2);
}

static void testPigTwoDice()
{
    Script s;
    PigGame &g = g_pig;
    g.reset(PigVariant::TWO_DICE);
    g.clearPlayers();
    g.addPlayer("A");
    g.addPlayer("B");
    g.setRng(scripted, &s);
    g.start();
    CHECK_EQ(g.numDice(), 2);
    s.set({3, 4, 5, 1});
    g.roll();
    CHECK_EQ(g.turnTotal(), 7);
    g.roll();   // single 1
    CHECK_EQ(g.hasBusted(), 1);
    CHECK_EQ(g.lostAll(), 0);
    g.passTurn();
    s.set({6, 6});
    g.roll();
    g.bank();   // B = 12
    s.set({5, 5});
    g.roll();
    g.bank();   // A = 10
    CHECK_EQ(g.total(0), 10);
    s.set({1, 1});
    g.roll();   // B: double ones -> total wiped
    CHECK_EQ(g.lostAll(), 1);
    CHECK_EQ(g.total(1), 0);
}

static int fs(std::initializer_list<int> d, int *used = nullptr)
{
    uint8_t v[6];
    int n = 0;
    for (int x : d) v[n++] = (uint8_t)x;
    return FarkleGame::scoreSet(v, n, used);
}

static void testFarkleScoring()
{
    int u = 0;
    CHECK_EQ(fs({1}), 100);
    CHECK_EQ(fs({5}), 50);
    CHECK_EQ(fs({2, 3, 4, 6}), 0);
    CHECK_EQ(fs({2, 2, 2}), 200);
    CHECK_EQ(fs({1, 1, 1}), 1000);
    CHECK_EQ(fs({4, 4, 4, 4}), 800);
    CHECK_EQ(fs({1, 1, 1, 1}), 2000);
    CHECK_EQ(fs({3, 3, 3, 3, 3}), 900);
    CHECK_EQ(fs({6, 6, 6, 6, 6, 6}), 2400);
    CHECK_EQ(fs({1, 2, 3, 4, 5, 6}, &u), 1500);
    CHECK_EQ(u, 6);
    CHECK_EQ(fs({2, 2, 3, 3, 6, 6}, &u), 750);
    CHECK_EQ(u, 6);
    CHECK_EQ(fs({1, 1, 5, 5, 3, 3}), 750);   // Three pairs beats 300
    CHECK_EQ(fs({1, 5, 2, 2, 2, 3}, &u), 350);
    CHECK_EQ(u, 5);
    CHECK_EQ(fs({1, 3}, &u), 100);
    CHECK_EQ(u, 1);
}

static void testFarkleTurn()
{
    Script s;
    FarkleGame &g = g_fk;
    g.reset();
    g.clearPlayers();
    g.addPlayer("A");
    g.addPlayer("B");
    g.setRng(scripted, &s);
    g.start();
    CHECK_EQ(g.canRoll(), 1);
    s.set({1, 2, 3, 4, 6, 6});
    g.roll();
    CHECK_EQ(g.hasFarkled(), 0);
    CHECK_EQ(g.canRoll(), 0);     // Must hold a scoring die first
    g.toggleHold(1);              // 2 -> invalid
    CHECK_EQ(g.heldValid(), 0);
    CHECK_EQ(g.heldScore(), 0);
    g.toggleHold(1);
    g.toggleHold(0);              // 1
    CHECK_EQ(g.heldScore(), 100);
    CHECK_EQ(g.canBank(), 0);     // Not opened: 100 < 500
    CHECK_EQ(g.canRoll(), 1);
    s.set({5, 5, 5, 2, 3});
    g.roll();
    CHECK_EQ(g.turnTotal(), 100);
    CHECK_EQ(g.locked(0), 1);
    CHECK_EQ(g.canHold(0), 0);
    g.toggleHold(1);
    g.toggleHold(2);
    g.toggleHold(3);
    CHECK_EQ(g.heldScore(), 500);
    CHECK_EQ(g.canBank(), 1);
    CHECK_EQ(g.bank(), 1);
    CHECK_EQ(g.player(0).total, 600);
    CHECK_EQ(g.player(0).opened, 1);
    CHECK_EQ(g.currentPlayer(), 1);

    // Farkle
    s.set({2, 3, 4, 6, 6, 2});
    g.roll();
    CHECK_EQ(g.hasFarkled(), 1);
    CHECK_EQ(g.canRoll(), 0);
    CHECK_EQ(g.canBank(), 0);
    CHECK_EQ(g.passTurn(), 1);
    CHECK_EQ(g.currentPlayer(), 0);

    // Hot dice: straight -> all six again
    s.set({1, 2, 3, 4, 5, 6});
    g.roll();
    for (int i = 0; i < 6; i++) g.toggleHold(i);
    CHECK_EQ(g.heldScore(), 1500);
    CHECK_EQ(g.hotDice(), 1);
    s.set({1, 1, 1, 2, 3, 4});
    g.roll();
    CHECK_EQ(g.turnTotal(), 1500);
    for (int i = 0; i < 6; i++) CHECK_EQ(g.locked(i), 0);
    g.toggleHold(0);
    g.toggleHold(1);
    g.toggleHold(2);
    CHECK_EQ(g.heldScore(), 1000);
    g.bank();
    CHECK_EQ(g.player(0).total, 3100);
}

static void testFarkleEndGame()
{
    Script s;
    FarkleGame &g = g_fk;
    g.reset();
    g.clearPlayers();
    g.addPlayer("A");
    g.addPlayer("B");
    g.addPlayer("C");
    g.setRng(scripted, &s);
    g.start();
    // A: six 6s = 2400 per hold, hot dice repeatedly until >= 10000
    s.set({6});
    g.roll();
    for (int k = 0; k < 4; k++) {
        for (int i = 0; i < 6; i++) g.toggleHold(i);
        g.roll();
    }
    for (int i = 0; i < 6; i++) g.toggleHold(i);
    CHECK_EQ(g.turnTotal() + g.heldScore(), 12000);
    g.bank();
    CHECK_EQ(g.finalRound(), 1);
    CHECK_EQ(g.finished(), 0);
    s.set({2, 3, 4, 6, 6, 2});
    g.roll();   // B farkles
    g.passTurn();
    CHECK_EQ(g.finished(), 0);
    g.roll();   // C farkles
    g.passTurn();
    CHECK_EQ(g.finished(), 1);
    int order[8], places[8];
    g.ranking(order, places);
    CHECK_EQ(order[0], 0);
    CHECK_EQ(places[1], 2);
    CHECK_EQ(places[2], 2);   // B and C share 2nd
}

static void testScc()
{
    Script s;
    SccGame &g = g_scc;
    g.reset(2);
    g.clearPlayers();
    g.addPlayer("A");
    g.addPlayer("B");
    g.setRng(scripted, &s);
    g.start();
    CHECK_EQ(g.totalRounds(), 2);
    CHECK_EQ(g.canEnd(), 0);
    s.set({5, 4, 2, 3, 1});   // No ship: 5 and 4 do not count yet
    g.roll();
    CHECK_EQ(g.hasShip(), 0);
    CHECK_EQ(g.hasCaptain(), 0);
    CHECK_EQ(g.canHold(0), 0);
    CHECK_EQ(g.canEnd(), 0);
    s.set({6, 5, 4, 2, 3});   // All in one roll
    g.roll();
    CHECK_EQ(g.hasShip(), 1);
    CHECK_EQ(g.hasCaptain(), 1);
    CHECK_EQ(g.hasCrew(), 1);
    CHECK_EQ(g.cargoScore(), 5);
    CHECK_EQ(g.canEnd(), 1);
    CHECK_EQ(g.rollsLeft(), 1);
    CHECK_EQ(g.role(0), (int)SccRole::SHIP);
    CHECK_EQ(g.canHold(0), 0);
    CHECK_EQ(g.canHold(3), 1);
    g.toggleHold(4);          // keep the 3 (index 4)
    s.set({6});
    g.roll();                 // index 3 -> 6
    CHECK_EQ(g.cargoScore(), 9);
    CHECK_EQ(g.rollsLeft(), 0);
    CHECK_EQ(g.canRoll(), 0);
    g.endTurn();
    CHECK_EQ(g.player(0).total, 9);
    CHECK_EQ(g.player(0).scoreCount, 1);
    CHECK_EQ(g.currentPlayer(), 1);

    // B: crew before captain is not allowed
    s.set({6, 4, 4, 1, 1});
    g.roll();
    CHECK_EQ(g.hasShip(), 1);
    CHECK_EQ(g.hasCaptain(), 0);
    CHECK_EQ(g.hasCrew(), 0);
    s.set({2, 2, 2, 2});
    g.roll();
    g.roll();
    CHECK_EQ(g.canEnd(), 1);
    CHECK_EQ(g.cargoScore(), 0);
    g.endTurn();
    CHECK_EQ(g.player(1).total, 0);
    CHECK_EQ(g.round(), 2);

    // Round 2: early save
    s.set({6, 5, 4, 6, 6});
    g.roll();
    CHECK_EQ(g.cargoScore(), 12);
    g.endTurn();
    s.set({1, 1, 1, 1, 1});
    g.roll(); g.roll(); g.roll();
    g.endTurn();
    CHECK_EQ(g.finished(), 1);
    CHECK_EQ(g.player(0).total, 21);
    int order[8], places[8];
    g.ranking(order, places);
    CHECK_EQ(order[0], 0);
}

int main()
{
    testPigOneDie();
    testPigTwoDice();
    testFarkleScoring();
    testFarkleTurn();
    testFarkleEndGame();
    testScc();
    std::printf("%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
