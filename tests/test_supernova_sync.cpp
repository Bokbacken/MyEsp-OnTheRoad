// Host test for main/supernova_sync.cpp
// g++ -std=c++17 -Wall -Wextra -Werror -Imain main/supernova_sync.cpp tests/test_supernova_sync.cpp
#include <stdio.h>
#include <string.h>

#include "supernova_sync.h"

using namespace SupernovaSync;

static int fails = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)

int main()
{
    Store s;
    CHECK(lyricTime(s.find("A"), 5000) == 5000);

    // Whole-song shift, then a drift that starts at 60 s and only affects the rest.
    CHECK(s.adjust("A", 0, 500));
    CHECK(s.adjust("A", 60000, -1000));
    const Entry *e = s.find("A");
    CHECK(e && offsetAt(*e, 10000) == 500 && offsetAt(*e, 59999) == 500 && offsetAt(*e, 60000) == -500);
    CHECK(lyricTime(e, 10000) == 9500 && lyricTime(e, 70000) == 70500);
    CHECK(lyricTime(e, 100) == 0);   // Never negative

    // Presses close together merge; cancelling out removes the entry
    CHECK(s.adjust("A", 61000, -100));
    CHECK(s.find("A")->adj.size() == 2 && offsetAt(*s.find("A"), 90000) == -600);
    CHECK(s.adjust("B", 5000, 100) && s.adjust("B", 5500, -100) && !s.find("B"));
    CHECK(s.adjust("B", 9000, 100) && s.adjust("B", 8000, 100));   // Earlier press pulls the entry back
    CHECK(s.find("B")->adj.size() == 1 && offsetAt(*s.find("B"), 8000) == 200);
    s.resetAdjustments("B");
    CHECK(!s.adjust("A", 1, 0) && !s.adjust("a/b", 1, 100) && !s.adjust("", 1, 100));
    CHECK(!s.adjust("A", 30000, 2 * MAX_TOTAL_MS));   // Total would exceed the limit

    CHECK(s.setBroken("C", true) && s.broken("C") && !s.broken("A"));
    s.resetAdjustments("A");
    CHECK(!s.find("A"));

    CHECK(s.adjust("A", 0, 250) && s.adjust("A", 90000, 750));
    const std::string text = s.encode();
    Store t;
    CHECK(t.decode(text.data(), text.size()));
    CHECK(t.broken("C") && t.find("A") && t.find("A")->adj.size() == 2 && offsetAt(*t.find("A"), 100000) == 1000);
    CHECK(t.encode() == text);
    CHECK(s.setBroken("C", false) && !s.find("C"));

    const char *bad = "good\tB\t\nnotab\nx\t\t5:abc,9999999999:1,-1:5,7:100\n";
    CHECK(t.decode(bad, strlen(bad)) && t.broken("good") && t.find("x") && t.find("x")->adj.size() == 1 &&
          t.find("x")->adj[0].posMs == 7);

    if (!fails) printf("OK\n");
    return fails ? 1 : 0;
}
