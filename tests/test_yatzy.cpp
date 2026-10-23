// Host unit tests for game_yatzy (no Arduino needed):
//   g++ -std=c++17 -Wall -Wextra -I MyEsp_OnTheRoad tests/test_yatzy.cpp MyEsp_OnTheRoad/game_yatzy.cpp -o test_yatzy && ./test_yatzy
#include <cstdio>
#include <cstring>
#include <initializer_list>

#include "game_yatzy.h"

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

using C = YatzyCategory;
static const YatzyVariant S = YatzyVariant::STANDARD, M = YatzyVariant::MAXI;

static int sc(YatzyVariant v, C c, std::initializer_list<int> d)
{
    uint8_t dice[6];
    int i = 0;
    for (int x : d) dice[i++] = (uint8_t)x;
    return Yatzy::calculateScore(v, c, dice);
}

static void testStandardScoring()
{
    CHECK_EQ(sc(S, C::ONES, {1, 1, 2, 3, 1}), 3);
    CHECK_EQ(sc(S, C::SIXES, {6, 6, 6, 2, 6}), 24);
    CHECK_EQ(sc(S, C::ONE_PAIR, {3, 3, 5, 5, 1}), 10);
    CHECK_EQ(sc(S, C::ONE_PAIR, {1, 2, 3, 4, 5}), 0);
    CHECK_EQ(sc(S, C::TWO_PAIRS, {3, 3, 5, 5, 1}), 16);
    CHECK_EQ(sc(S, C::TWO_PAIRS, {4, 4, 4, 4, 1}), 0);       // Same value is not two pairs
    CHECK_EQ(sc(S, C::THREE_OF_A_KIND, {2, 2, 2, 5, 6}), 6);
    CHECK_EQ(sc(S, C::FOUR_OF_A_KIND, {5, 5, 5, 5, 5}), 20);
    CHECK_EQ(sc(S, C::SMALL_STRAIGHT, {5, 3, 1, 2, 4}), 15);
    CHECK_EQ(sc(S, C::SMALL_STRAIGHT, {2, 3, 4, 5, 6}), 0);
    CHECK_EQ(sc(S, C::LARGE_STRAIGHT, {6, 5, 4, 3, 2}), 20);
    CHECK_EQ(sc(S, C::FULL_HOUSE, {2, 2, 3, 3, 3}), 13);
    CHECK_EQ(sc(S, C::FULL_HOUSE, {3, 3, 3, 3, 3}), 0);      // Kåk needs two different values
    CHECK_EQ(sc(S, C::CHANCE, {1, 2, 3, 4, 6}), 16);
    CHECK_EQ(sc(S, C::YATZY, {4, 4, 4, 4, 4}), 50);
    CHECK_EQ(sc(S, C::YATZY, {4, 4, 4, 4, 3}), 0);
    CHECK_EQ(Yatzy::bonusThreshold(S), 63);
    CHECK_EQ(Yatzy::bonusPoints(S), 50);
    int n;
    Yatzy::categories(S, n);
    CHECK_EQ(n, 15);
}

static void testMaxiScoring()
{
    CHECK_EQ(sc(M, C::THREE_PAIRS, {1, 1, 4, 4, 6, 6}), 22);
    CHECK_EQ(sc(M, C::THREE_PAIRS, {4, 4, 4, 4, 6, 6}), 0);
    CHECK_EQ(sc(M, C::TWO_PAIRS, {6, 6, 5, 5, 4, 4}), 22);   // Best two pairs
    CHECK_EQ(sc(M, C::FIVE_OF_A_KIND, {3, 3, 3, 3, 3, 1}), 15);
    CHECK_EQ(sc(M, C::FULL_STRAIGHT, {6, 1, 5, 2, 4, 3}), 21);
    CHECK_EQ(sc(M, C::SMALL_STRAIGHT, {1, 2, 3, 4, 5, 5}), 15);
    CHECK_EQ(sc(M, C::FULL_HOUSE, {6, 6, 6, 5, 5, 5}), 28);  // Best 3+2 = 6+6+6+5+5
    CHECK_EQ(sc(M, C::VILLA, {2, 2, 2, 5, 5, 5}), 21);
    CHECK_EQ(sc(M, C::VILLA, {2, 2, 2, 2, 2, 2}), 0);
    CHECK_EQ(sc(M, C::TOWER, {1, 1, 1, 1, 6, 6}), 16);
    CHECK_EQ(sc(M, C::CHANCE, {6, 6, 6, 6, 6, 5}), 35);
    CHECK_EQ(sc(M, C::YATZY, {2, 2, 2, 2, 2, 2}), 100);
    CHECK_EQ(Yatzy::bonusThreshold(M), 84);
    CHECK_EQ(Yatzy::bonusPoints(M), 100);
    int n;
    Yatzy::categories(M, n);
    CHECK_EQ(n, 20);
}

// Deterministic die source for game-flow tests.
struct Seq { const uint8_t *v; int n, i; };
static uint8_t seqRng(void *ctx)
{
    Seq *s = (Seq *)ctx;
    uint8_t r = s->v[s->i % s->n];
    s->i++;
    return r;
}

static void testGameFlow()
{
    static const uint8_t vals[] = {6, 6, 6, 6, 6};
    Seq seq{vals, 5, 0};
    YatzyGame g;
    g.reset(S, YatzyMode::NORMAL);
    g.setRng(seqRng, &seq);
    CHECK_EQ(g.addPlayer("Kent"), 1);
    CHECK_EQ(g.addPlayer("Anna"), 1);
    g.start();
    CHECK_EQ(g.totalRounds(), 15);
    CHECK_EQ(g.canScore(C::YATZY), 0);                       // Must roll first
    CHECK_EQ(g.roll(), 1);
    CHECK_EQ(g.rollsLeft(), 2);
    CHECK_EQ(g.toggleHold(0), 1);
    CHECK_EQ(g.held(0), 1);
    YatzySuggestion sug[YATZY_CATEGORY_COUNT];
    int n = g.getSuggestedCategories(sug, YATZY_CATEGORY_COUNT);
    CHECK_EQ(n, 15);
    CHECK_EQ((int)sug[0].category, (int)C::YATZY);
    CHECK_EQ(g.score(C::YATZY), 1);
    CHECK_EQ(g.player(0).scores[(int)C::YATZY], 50);
    CHECK_EQ(g.currentPlayer(), 1);
    CHECK_EQ(g.hasRolled(), 0);

    // Play the whole game out: every player writes the first free category each turn.
    int guard = 0;
    while (!g.finished() && guard++ < 100) {
        g.roll();
        int cnt;
        const YatzyCategory *cats = Yatzy::categories(S, cnt);
        for (int i = 0; i < cnt; i++) if (g.canScore(cats[i])) { g.score(cats[i]); break; }
    }
    CHECK_EQ(g.finished(), 1);
    CHECK_EQ(g.upperSum(0), 30);                             // Five sixes in "Sexor", others 0
    int order[8], places[8];
    CHECK_EQ(g.ranking(order, places), 2);
    CHECK_EQ(places[0], 1);
}

static void testOrderedAndSavedRolls()
{
    static const uint8_t vals[] = {1, 2, 3, 4, 5, 6};
    Seq seq{vals, 6, 0};
    YatzyGame g;
    g.reset(M, YatzyMode::ORDERED, true);
    g.setRng(seqRng, &seq);
    g.addPlayer("A");
    g.start();
    g.roll();
    CHECK_EQ(g.canScore(C::TWOS), 0);                        // Ordered: Ettor first
    CHECK_EQ(g.canScore(C::ONES), 1);
    g.score(C::ONES);                                        // 2 rolls unused -> saved
    // Single player: the next turn starts immediately and consumes the saved rolls
    CHECK_EQ(g.player(0).savedRolls, 0);
    CHECK_EQ(g.rollsLeft(), 5);                              // Next turn: 3 + 2 saved
    CHECK_EQ((int)g.nextOrderedCategory(0), (int)C::TWOS);
}

int main()
{
    testStandardScoring();
    testMaxiScoring();
    testGameFlow();
    testOrderedAndSavedRolls();
    std::printf("%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
