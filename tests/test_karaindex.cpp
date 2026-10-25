// Host test for main/kara_index.{h,cpp} (v0.13.7 song library + songs.idx)
#include <string.h>

#include "check.h"
#include "kara_index.h"

using namespace KaraIndex;

static Library sample()
{
    Library l;
    l.add("ABBA_Waterloo.kar", "Waterloo", "ABBA", 1000);
    l.add("Svenskt/\xc3\x96sten/Kent_Musik.kar", "Musik", "Kent", 2000);                 // Östen
    l.add("Svenskt/\xc3\x85sa_Visa.mid", "Visa", "\xc3\x85sa", 300);                     // Åsa
    l.add("Svenskt/\xc3\x84ngel_Sång.mid", "S\xc3\xa5ng", "\xc3\x84ngel", 400);          // Ängel
    l.add("Pop/80s/Europe_The Final Countdown.kar", "The Final Countdown", "Europe", 5000);
    l.add("Pop/Zed_Zebra.midi", "Zebra", "Zed", 10);
    l.add("Pop/80s/Deep/Bowie_Heroes.rmi", "Heroes", "Bowie", 20);
    l.add("Instrumental.mid", "Instrumental", "", 30);
    l.add("1999.kar", "1999", "", 40);
    l.sort();
    return l;
}

int main()
{
    // Ranks and collation (Swedish order: ... z å ä ö)
    CHECK(firstRank("abc") == 30);
    CHECK(firstRank("Zorro") == 55);
    CHECK(firstRank("\xc3\x85sa") == 56);
    CHECK(firstRank("\xc3\xa4ngel") == 57);
    CHECK(firstRank("\xc3\x96sten") == 58);
    CHECK(firstRank("\xc3\xa9l\xc3\xa9phant") == 34);   // é = e
    CHECK(firstRank("'Til") == 49);                     // Leading symbols are skipped
    CHECK(firstRank("9 to 5") == 19);
    CHECK(firstRank("") == 0);
    CHECK(collate("Zebra", "\xc3\x85sa") < 0);
    CHECK(collate("\xc3\x85sa", "\xc3\x84ngel") < 0);
    CHECK(collate("\xc3\x84ngel", "\xc3\x96sten") < 0);
    CHECK(collate("abc", "abd") < 0);
    CHECK(collate("ab", "abc") < 0);
    CHECK(fold("ABC \xc3\x85\xc3\x84\xc3\x96") == "abc \xc3\xa5\xc3\xa4\xc3\xb6");

    // Path checks / extensions
    CHECK(safeRel("a.kar"));
    CHECK(safeRel("Pop/80s/a.kar"));
    CHECK(!safeRel(""));
    CHECK(!safeRel(nullptr));
    CHECK(!safeRel("/abs.kar"));
    CHECK(!safeRel("../x.kar"));
    CHECK(!safeRel("a/../x.kar"));
    CHECK(!safeRel("a/./x.kar"));
    CHECK(!safeRel("a//x.kar"));
    CHECK(!safeRel("a\\x.kar"));
    CHECK(!safeRel("dir/"));
    CHECK(safeRel("..hidden.kar"));
    CHECK(songExt("a.KAR") && songExt("b.mid") && songExt("c.Midi") && songExt("d.rmi"));
    CHECK(!songExt("a.mp3") && !songExt("kar") && !songExt(nullptr));

    Library l = sample();
    CHECK(l.size() == 9);
    // Sorted by artist (or title without artist): 1999, ABBA, Bowie, Europe, Instrumental, Kent, Zed, Åsa, Ängel
    CHECK(!strcmp(l.sortName(0), "1999"));
    CHECK(!strcmp(l.sortName(1), "ABBA"));
    CHECK(!strcmp(l.sortName(2), "Bowie"));
    CHECK(!strcmp(l.sortName(4), "Instrumental"));
    CHECK(!strcmp(l.sortName(6), "Zed"));
    CHECK(!strcmp(l.sortName(7), "\xc3\x85sa"));
    CHECK(!strcmp(l.sortName(8), "\xc3\x84ngel"));
    CHECK(l.dir(0) == "");
    CHECK(l.dir(2) == "Pop/80s/Deep");

    // Folders
    std::vector<std::string> d = l.subdirs("");
    CHECK(d.size() == 2 && d[0] == "Pop" && d[1] == "Svenskt");
    d = l.subdirs("Pop");
    CHECK(d.size() == 1 && d[0] == "80s");
    d = l.subdirs("Pop/80s");
    CHECK(d.size() == 1 && d[0] == "Deep");
    CHECK(l.subdirs("Pop/80s/Deep").empty());
    CHECK(l.subdirs("Po").empty());   // Prefix of a folder name is not the folder
    d = l.subdirs("Svenskt");
    CHECK(d.size() == 1 && d[0] == "\xc3\x96sten");

    CHECK(l.songsIn("").size() == 9);
    CHECK(l.songsIn("Pop").size() == 3);
    CHECK(l.songsIn("Pop/80s").size() == 2);
    CHECK(l.songsIn("Pop/80s/Deep").size() == 1);
    CHECK(l.songsIn("Po").empty());
    CHECK(l.songsIn("Svenskt").size() == 3);
    CHECK(l.songsIn("Nope").empty());

    // Search: words in any order, case-insensitive incl. å ä ö, across folders
    CHECK(l.search("waterloo").size() == 1);
    CHECK(l.search("abba WATER").size() == 1);
    CHECK(l.search("final europe").size() == 1);
    CHECK(l.search("\xc3\xa5sa").size() == 1);           // "åsa" finds "Åsa"
    CHECK(l.search("S\xc3\x85NG").size() == 1);           // "SÅNG" finds "Sång"
    CHECK(l.search("e").size() >= 4);
    CHECK(l.search("").empty());
    CHECK(l.search("   ").empty());
    CHECK(l.search("xyz").empty());
    CHECK(l.search("Pop").empty());   // Folder names are not searched

    CHECK(l.find("Pop/Zed_Zebra.midi") >= 0);
    CHECK(l.find("Zed_Zebra.midi") == -1);

    // A-Ö jump
    std::vector<uint16_t> all = l.songsIn("");
    CHECK(jumpIndex(l, all, 30) == 1);   // A -> ABBA
    CHECK(jumpIndex(l, all, 32) == 3);   // C -> Europe (first >= C)
    CHECK(jumpIndex(l, all, 56) == 7);   // Å
    CHECK(jumpIndex(l, all, 58) == 9);   // Ö: none
    CHECK(jumpIndex(l, all, 0) == 0);

    // Index round trip
    Header h;
    h.scanTime = 1793000000u;
    h.freeBytes = 123456789012ull;
    h.rootMtime = 0x12345678u;
    std::vector<uint8_t> buf;
    l.encode(h, buf);
    Library l2;
    Header h2;
    CHECK(l2.decode(buf.data(), buf.size(), h2));
    CHECK(h2.version == VERSION && h2.scanTime == h.scanTime && h2.freeBytes == h.freeBytes);
    CHECK(h2.rootMtime == h.rootMtime && h2.count == 9 && l2.size() == 9);
    for (size_t i = 0; i < l.size(); i++) {
        CHECK(!strcmp(l.rel(i), l2.rel(i)));
        CHECK(!strcmp(l.title(i), l2.title(i)));
        CHECK(!strcmp(l.artist(i), l2.artist(i)));
        CHECK(l.fileSize(i) == l2.fileSize(i));
        CHECK(l.dir(i) == l2.dir(i));
    }
    // Corruption is detected
    std::vector<uint8_t> bad = buf;
    bad[30] ^= 1;
    CHECK(!l2.decode(bad.data(), bad.size(), h2) && l2.size() == 0);
    CHECK(!l2.decode(buf.data(), buf.size() - 1, h2));
    CHECK(!l2.decode(buf.data(), 10, h2));
    CHECK(!l2.decode(nullptr, 0, h2));
    bad = buf;
    bad[4] = 99;   // Other version (checksum fixed up)
    {
        uint32_t s = 2166136261u;
        for (size_t i = 0; i + 4 < bad.size(); i++) s = (s ^ bad[i]) * 16777619u;
        for (int i = 0; i < 4; i++) bad[bad.size() - 4 + i] = (uint8_t)(s >> (8 * i));
    }
    CHECK(!l2.decode(bad.data(), bad.size(), h2));
    // Empty library
    Library e;
    e.encode(Header{}, buf);
    CHECK(l2.decode(buf.data(), buf.size(), h2) && l2.size() == 0 && h2.count == 0);

    // Limits
    Library big;
    char name[32];
    for (size_t i = 0; i < MAX_SONGS + 5; i++) {
        snprintf(name, sizeof name, "d%u/s%u.kar", (unsigned)(i % 7), (unsigned)i);
        big.add(name, name, "", 1);
    }
    CHECK(big.size() == MAX_SONGS && big.full());
    CHECK(!big.add("x.kar", "x", "", 1));
    big.sort();
    CHECK(big.subdirs("").size() == 7);
    big.encode(Header{}, buf);
    CHECK(l2.decode(buf.data(), buf.size(), h2) && l2.size() == MAX_SONGS);
    Library t;
    CHECK(!t.add("../evil.kar", "e", "", 1));
    std::string longp(300, 'a');
    CHECK(!t.add(longp.c_str(), "e", "", 1));
    std::string longt(400, 't');
    CHECK(t.add("ok.kar", longt.c_str(), "", 1) && strlen(t.title(0)) == 255);
    CHECK_DONE();
}
