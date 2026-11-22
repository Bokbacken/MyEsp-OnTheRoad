// Host test for main/supernova_index.cpp
// g++ -std=c++17 -Wall -Wextra -Werror -Imain tests/test_supernova_index.cpp main/supernova_index.cpp main/kara_index.cpp
#include <stdio.h>
#include <string.h>

#include "supernova_index.h"

using namespace SupernovaIndex;

static int fails = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)

static void fill(Library &l)
{
    l.add("ABBA - Waterloo", "ABBA", "Waterloo", "Pop", "Swedish", 1974, 10);
    l.add("Queen - Radio Ga Ga", "Queen", "Radio Ga Ga", "Rock, Pop", "English", 1984, 11);
    l.add("Åsa - Öl", "Åsa", "Öl", "", "", 0, 12);
    l.add("Whatever", "", "Whatever", "rock", "English", 1985, 13);
}

int main()
{
    Tags t;
    const char *txt = "\xEF\xBB\xBF#TITLE:Waterloo\r\n#artist: ABBA \r\n#GENRE:Pop\n#YEAR:1974\n#LANGUAGE:Swedish\n: 0 4 0 la\n#TITLE:Late\n";
    parseTags(txt, strlen(txt), t);
    CHECK(t.title == "Waterloo" && t.artist == "ABBA" && t.genre == "Pop" && t.year == 1974 && t.language == "Swedish");
    parseTags("#YEAR:abc\n#TITLE:\xFF\n", 19, t);
    CHECK(t.year == 0 && t.title.empty());
    parseTags(nullptr, 0, t);
    CHECK(t.artist.empty());
    const char *aud = "#MP3:old.mp3\n#AUDIO:Song.mp3\n#instrumental:Song [Kar].mp3\n#VOCALS:v.mp3\n: 0 4 0 la\n";
    parseTags(aud, strlen(aud), t);
    CHECK(t.audio == "Song.mp3" && t.mp3 == "old.mp3" && t.instrumental == "Song [Kar].mp3" && t.vocals == "v.mp3");

    Library curly;
    curly.add("Avril - Nobody's Home", "Avril Lavigne", "Nobody\xE2\x80\x99s Home \xE2\x80\x93 \xE2\x80\x9Cx\xE2\x80\x9D", "", "", 0, 1);
    CHECK(strcmp(curly.title(0), "Nobody's Home - \"x\"") == 0);

    std::string a, ti;
    namesFromFolder("Queen - Radio Ga Ga", a, ti);
    CHECK(a == "Queen" && ti == "Radio Ga Ga");
    namesFromFolder("Solo", a, ti);
    CHECK(a.empty() && ti == "Solo");

    Library l;
    fill(l);
    CHECK(!l.add("a/b", "", "", "", "", 0, 0));
    CHECK(l.find("Whatever") == 3 && l.find("none") == -1);

    Query q;
    auto v = l.view(q);   // Artist sort: ABBA, Queen, Whatever (title as artist), Åsa
    CHECK(v.size() == 4 && v[0] == 0 && v[1] == 1 && v[2] == 3 && v[3] == 2);
    q.sort = Sort::YEAR;
    v = l.view(q);
    CHECK(v[0] == 0 && v[1] == 1 && v[2] == 3 && v[3] == 2);   // Unknown year last
    q.sort = Sort::ARTIST;
    q.kind = FilterKind::GENRE;
    q.value = "pop";
    CHECK(l.view(q).size() == 2);
    q.kind = FilterKind::DECADE;
    q.value = "1980";
    CHECK(l.view(q).size() == 2);
    q.kind = FilterKind::NONE;
    q.text = "radio GA";
    v = l.view(q);
    CHECK(v.size() == 1 && v[0] == 1);
    q.text = "åsa öl";
    CHECK(l.view(q).size() == 1);

    auto g = l.values(FilterKind::GENRE);
    CHECK(g.size() == 2 && g[0].name == "Pop" && g[0].count == 2 && g[1].count == 2);
    auto d = l.values(FilterKind::DECADE);
    CHECK(d.size() == 2 && d[0].key == "1970" && d[0].name == "1970s" && d[1].count == 2);

    q = Query();
    v = l.view(q);
    CHECK(jumpIndex(l, v, Sort::ARTIST, KaraIndex::firstRank("Q")) == 1);
    CHECK(jumpIndex(l, v, Sort::ARTIST, 56) == 3);
    CHECK(l.rank(0, Sort::YEAR) == 0);

    std::vector<uint8_t> buf;
    l.encode(buf);
    Library l2;
    CHECK(l2.decode(buf.data(), buf.size()));
    CHECK(l2.size() == 4 && !strcmp(l2.title(2), "Öl") && l2.year(1) == 1984 && l2.mtime(3) == 13);
    buf[10] ^= 0x55;
    CHECK(!l2.decode(buf.data(), buf.size()) && l2.size() == 0);
    CHECK(!l2.decode(buf.data(), 3));

    if (!fails) printf("OK\n");
    return fails ? 1 : 0;
}
