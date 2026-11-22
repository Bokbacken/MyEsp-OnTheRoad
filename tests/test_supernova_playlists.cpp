// Host test for supernova_playlists.cpp
#include <stdio.h>
#include <stdlib.h>

#include "supernova_playlists.h"

using namespace SupernovaPlaylists;

#define CHECK(c)                                                         \
    do {                                                                 \
        if (!(c)) {                                                      \
            printf("FAIL line %d: %s\n", __LINE__, #c);                  \
            exit(1);                                                     \
        }                                                                \
    } while (0)

int main()
{
    Store s;
    uint16_t a = 0, b = 0;
    CHECK(s.create("  Fest  ", a) == Err::OK && a == 1);
    CHECK(s.find(a)->name == "Fest");
    CHECK(s.create("fest", b) == Err::DUPLICATE_NAME);
    CHECK(s.create("", b) == Err::BAD_NAME);
    CHECK(s.create(std::string(MAX_NAME + 1, 'x'), b) == Err::BAD_NAME);
    CHECK(s.create("Rock", b) == Err::OK && b == 2);

    CHECK(s.add(a, "ABBA - Waterloo") == Err::OK);
    CHECK(s.add(a, "Queen - Radio Ga Ga") == Err::OK);
    CHECK(s.add(a, "Avril Lavigne - Nobody's Home") == Err::OK);
    CHECK(s.add(a, "ABBA - Waterloo") == Err::OK);   // Duplicate is a no-op
    CHECK(s.find(a)->folders.size() == 3);
    CHECK(s.add(a, "../evil") == Err::BAD_FOLDER);
    CHECK(s.add(a, "a/b") == Err::BAD_FOLDER);
    CHECK(s.add(99, "x") == Err::NO_LIST);

    CHECK(s.move(a, "Queen - Radio Ga Ga", -1) == Err::OK);
    CHECK(s.find(a)->folders[0] == "Queen - Radio Ga Ga");
    CHECK(s.move(a, "Queen - Radio Ga Ga", -1) == Err::OK);   // Already first: unchanged
    CHECK(s.find(a)->folders[0] == "Queen - Radio Ga Ga");
    CHECK(s.move(a, "nope", 1) == Err::NO_SONG);
    CHECK(s.removeSong(a, "ABBA - Waterloo") == Err::OK);
    CHECK(s.removeSong(a, "ABBA - Waterloo") == Err::NO_SONG);

    CHECK(s.rename(b, "Fest") == Err::DUPLICATE_NAME);
    CHECK(s.rename(b, "Rock 80") == Err::OK);

    const std::string text = s.encode();
    Store t;
    CHECK(t.decode(text.data(), text.size()));
    CHECK(t.lists().size() == 2);
    CHECK(t.find(a)->folders.size() == 2 && t.find(a)->folders[0] == "Queen - Radio Ga Ga");
    CHECK(t.find(b)->name == "Rock 80");
    CHECK(t.encode() == text);

    // Removing a list does not reuse the highest id while it is still taken; ids stay unique
    CHECK(s.remove(a) == Err::OK && s.remove(a) == Err::NO_LIST);
    uint16_t c = 0;
    CHECK(s.create("Ny", c) == Err::OK && c == 3);

    // Corrupt input is skipped, not fatal
    const char junk[] = "x\n#\t0\tZero\n#\t5\t\nsong\n#\t7\tOk\r\nA\r\nA\nB\n#\t7\tAgain\nC\n";
    Store j;
    CHECK(j.decode(junk, sizeof(junk) - 1));
    CHECK(j.lists().size() == 1 && j.find(7) && j.find(7)->folders.size() == 2);

    for (size_t i = 0; i < MAX_LISTS - 2; i++) {
        uint16_t id;
        CHECK(s.create("L" + std::to_string(i), id) == Err::OK);
    }
    uint16_t id;
    CHECK(s.create("Overflow", id) == Err::FULL);
    printf("OK\n");
    return 0;
}
