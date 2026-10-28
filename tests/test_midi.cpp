// Host test for components/otr_midi (SMF parser, lyrics, melody detection, registry, player).
// Optional: pass .mid/.kar paths on the command line to also parse real files.
#include <stdio.h>
#include <string.h>

#include <string>
#include <vector>

#include "check.h"
#include "otr_midi.h"

using namespace Midi;

// ---- Tiny SMF writer -----------------------------------------------------------------------------
struct Trk {
    std::vector<uint8_t> b;
    void var(uint32_t v)
    {
        uint8_t tmp[5];
        int n = 0;
        tmp[n++] = v & 0x7F;
        while (v >>= 7) tmp[n++] = (uint8_t)(0x80 | (v & 0x7F));
        while (n) b.push_back(tmp[--n]);
    }
    void ev(uint32_t dt, std::initializer_list<uint8_t> bytes)
    {
        var(dt);
        b.insert(b.end(), bytes);
    }
    void meta(uint32_t dt, uint8_t type, const std::string &s)
    {
        var(dt);
        b.push_back(0xFF);
        b.push_back(type);
        var((uint32_t)s.size());
        b.insert(b.end(), s.begin(), s.end());
    }
    void end() { meta(0, 0x2F, ""); }
};

static void be32(std::vector<uint8_t> &o, uint32_t v)
{
    for (int i = 3; i >= 0; i--) o.push_back((uint8_t)(v >> (i * 8)));
}

static std::vector<uint8_t> smf(int format, uint16_t division, const std::vector<Trk> &tracks)
{
    std::vector<uint8_t> o = {'M', 'T', 'h', 'd'};
    be32(o, 6);
    o.push_back(0), o.push_back((uint8_t)format);
    o.push_back(0), o.push_back((uint8_t)tracks.size());
    o.push_back((uint8_t)(division >> 8)), o.push_back((uint8_t)division);
    for (const Trk &t : tracks) {
        o.insert(o.end(), {'M', 'T', 'r', 'k'});
        be32(o, (uint32_t)t.b.size());
        o.insert(o.end(), t.b.begin(), t.b.end());
    }
    return o;
}

struct Rec : Sink {
    std::vector<uint32_t> msgs;
    int resets = 0;
    void midi(uint8_t s, uint8_t a, uint8_t b) override { msgs.push_back((uint32_t)s << 16 | (uint32_t)a << 8 | b); }
    void reset() override { resets++; }
};

// Karaoke-like format 1 file: conductor, words (FF 01 Soft Karaoke), melody ch 3, chords ch 0, drums
static std::vector<uint8_t> karFile()
{
    Trk cond, words, mel, chords, drums;
    cond.meta(0, 0x51, std::string("\x07\xA1\x20", 3));   // 500000 us / quarter = 120 bpm
    cond.meta(0, 0x01, "@KMIDI KARAOKE FILE");
    cond.meta(0, 0x01, "@TTestlåt");
    cond.meta(0, 0x01, "@TArtisten");
    cond.end();
    words.meta(0, 0x03, "Words");
    // Syllables at quarter notes (480 ticks), Latin-1 "Hå"
    const char *syl[] = {"\\H\xE5", "lå ", "/Vä", "gen ", "/Sist", " ord", "\\Ny ", "vers"};
    for (int i = 0; i < 8; i++) words.meta(i ? 480 : 480, 0x01, syl[i]);
    words.end();
    mel.meta(0, 0x03, "Melody");
    mel.ev(0, {0xC3, 73});
    for (int i = 0; i < 8; i++) {
        mel.ev(i ? 40 : 480, {0x93, (uint8_t)(60 + i), 100});
        mel.ev(440, {0x83, (uint8_t)(60 + i), 0});
    }
    mel.end();
    chords.meta(0, 0x03, "Piano");
    chords.ev(0, {0xC0, 0});
    chords.ev(0, {0xB0, 7, 90});
    for (int i = 0; i < 4; i++) {
        chords.ev(i ? 0 : 480, {0x90, 48, 80});
        chords.ev(0, {52, 80});   // Running status
        chords.ev(0, {55, 80});
        chords.ev(960, {0x90, 48, 0});
        chords.ev(0, {52, 0});
        chords.ev(0, {55, 0});
    }
    chords.end();
    drums.ev(480, {0x99, 36, 100});
    drums.ev(240, {0x89, 36, 0});
    drums.end();
    return smf(1, 480, {cond, words, mel, chords, drums});
}

static void testParseKar()
{
    std::vector<uint8_t> f = karFile();
    Song s;
    CHECK(parse(f.data(), f.size(), s) == Err::OK);
    // Karaoke releases the raw file before synth preparation; parsed data must be independent.
    memset(f.data(), 0, f.size());
    std::vector<uint8_t>().swap(f);
    CHECK(s.format == 1 && s.tracks == 5 && s.division == 480);
    CHECK(s.title == "Testlåt" || s.title == "Testl\xC3\xA5t");
    CHECK(s.artist == "Artisten");
    CHECK(s.lyricFromText && s.lyricTrack == 1);
    CHECK(s.syls.size() == 8);
    CHECK(s.lines.size() == 4);
    if (s.lines.size() == 4) {
        CHECK(s.lineText(0) == "Hålå ");
        CHECK(s.lineText(1) == "Vägen ");
        CHECK(s.lineText(2) == "Sist ord");
        CHECK(s.lineText(3) == "Ny vers");
        CHECK(s.lines[0].para && !s.lines[1].para && !s.lines[2].para && s.lines[3].para);
        CHECK(s.lines[0].startUs == 500000 && s.lines[1].startUs == 1500000);
        CHECK(s.lines[0].endUs == s.lines[1].startUs);
        CHECK(s.lines[3].endUs == s.lengthUs);
    }
    CHECK(s.lineAt(0) == -1);
    CHECK(s.lineAt(500000) == 0);
    CHECK(s.lineAt(1499999) == 0);
    CHECK(s.lineAt(1500000) == 1);
    CHECK(s.lineAt(99000000) == 3);
    CHECK(s.sylAt(1000000) == 1);
    std::string sung, rest;
    s.lineSplit(1, 3, sung, rest);
    CHECK(sung == "Vä" && rest == "gen ");
    s.lineSplit(9, 3, sung, rest);
    CHECK(sung.empty() && rest.empty());
    // Channels
    CHECK(s.channelMask == ((1u << 0) | (1u << 3) | (1u << 9)));
    CHECK(s.ch[3].notes == 8 && s.ch[3].maxPoly == 1 && s.ch[3].overlapPct == 0);
    CHECK(s.ch[3].program == 73 && !strcmp(s.ch[3].trackName, "Melody"));
    CHECK(s.ch[0].notes == 12 && s.ch[0].maxPoly == 3);
    CHECK(s.ch[3].lyricMatchPct == 100);
    CHECK(s.melody == 3);
    CHECK(!strcmp(gmName(73), "Flute"));
    CHECK(!strcmp(gmName(0), "Acoustic Grand Piano"));
    // Timing: last chord off at 480 + 4*960 ticks = 4.5 s, plus 0.5 s ring-out
    CHECK(s.lengthUs == 5000000);
}

// Format 0, FF 05 lyrics with CR / LF / CRLF, UTF-8 text, tempo change, no melody track name
static void testParseLyricFormat0()
{
    Trk t;
    t.meta(0, 0x03, "Song");
    t.ev(0, {0xC5, 0});
    const char *syl[] = {"Ett ", "två\r", "tre ", "fyra\r\n", "\r\n", "fem ", "sex\n", "  ", "\rsju"};
    for (int i = 0; i < 9; i++) {
        t.meta(i ? 0 : 96, 0x05, syl[i]);
        t.ev(0, {0x95, (uint8_t)(64 + i), 90});
        if (i == 3) t.meta(0, 0x51, std::string("\x0F\x42\x40", 3));   // 1000000 us / quarter
        t.ev(90, {0x85, (uint8_t)(64 + i), 0});
        t.var(6);   // Delta for the next event
        t.b.push_back(0xF0), t.b.push_back(1), t.b.push_back(0xF7);   // SysEx in between
    }
    t.end();
    std::vector<uint8_t> f = smf(0, 96, {t});
    Song s;
    CHECK(parse(f.data(), f.size(), s) == Err::OK);
    CHECK(!s.lyricFromText && s.utf8);
    CHECK(s.title == "Song");
    CHECK(s.lines.size() == 4);
    if (s.lines.size() == 4) {
        CHECK(s.lineText(0) == "Ett två");
        CHECK(s.lineText(1) == "tre fyra");
        CHECK(s.lineText(2) == "fem sex");
        CHECK(s.lineText(3) == "sju");
        CHECK(s.lines[2].para);   // CRLF + CRLF = paragraph
    }
    CHECK(s.melody == 5);
    CHECK(s.ch[5].lyricMatchPct == 100);
    // First syllable at 96 ticks @ 500000/96 -> 500000 us; tempo doubles after the 4th note-on
    CHECK(s.syls.size() >= 1 && s.syls[0].us == 500000);
}

static void testErrors()
{
    Song s;
    const uint8_t junk[] = "hello world, not a midi file";
    CHECK(parse(junk, sizeof(junk), s) == Err::NOT_MIDI);
    std::vector<uint8_t> f = karFile();
    CHECK(parse(f.data(), 10, s) == Err::NOT_MIDI);
    CHECK(parse(f.data(), MAX_FILE + 1, s) == Err::TOO_BIG);
    std::vector<uint8_t> f2 = f;
    f2[9] = 2;   // Format 2
    CHECK(parse(f2.data(), f2.size(), s) == Err::UNSUPPORTED);
    Trk t;
    t.meta(0, 0x05, "Bara text");
    t.end();
    std::vector<uint8_t> e = smf(0, 96, {t});
    CHECK(parse(e.data(), e.size(), s) == Err::EMPTY);
    // Truncated last track is tolerated
    std::vector<uint8_t> cut(f.begin(), f.end() - 20);
    CHECK(parse(cut.data(), cut.size(), s) == Err::OK);
    // RIFF RMID wrapper
    std::vector<uint8_t> r = {'R', 'I', 'F', 'F', 0, 0, 0, 0, 'R', 'M', 'I', 'D', 'd', 'a', 't', 'a', 0, 0, 0, 0};
    r.insert(r.end(), f.begin(), f.end());
    CHECK(parse(r.data(), r.size(), s) == Err::OK && s.melody == 3);
    CHECK(!strcmp(errName(Err::TOO_BIG), errName(Err::TOO_BIG)) && errName(Err::OK));
}

static void testUtf8AndNames()
{
    bool was = true;
    CHECK(toUtf8("\xC4lska", 5, &was) == "\xC3\x84lska" && !was);
    CHECK(toUtf8("\xC3\x84lska", 6, &was) == "\xC3\x84lska" && was);
    CHECK(toUtf8("\x93hi\x94", 4) == "\"hi\"");   // cp1252 quotes folded to ASCII (Latin-1 font)
    std::string t, a;
    nameFromFile("/sdcard/otr/karaoke/Ainbusk Singers_\xC4lska mej.mid", t, a);
    CHECK(a == "Ainbusk Singers" && t == "\xC3\x84lska mej");
    nameFromFile("All Saints_Black_Coffee.kar", t, a);
    CHECK(a == "All Saints" && t == "Black Coffee");
    nameFromFile("Plain.mid", t, a);
    CHECK(a.empty() && t == "Plain");
    nameFromFile("_x.mid", t, a);
    CHECK(a.empty() && t == " x");
    CHECK(crc32((const uint8_t *)"123456789", 9) == 0xCBF43926u);
}

static void testRegistry()
{
    Registry r;
    r.load("# comment\n"
           "8c5c36ba 51234 2 0 100 Abba_Another town.mid\r\n"
           "broken line\n"
           "9218be09 40000 3 -2 90 After shave_Macken.mid\n"
           "deadbeef 1 99 0 100 bad melody\n"
           "deadbeef 1 3 0 200 bad tempo\n");
    CHECK(r.size() == 2);
    const RegEntry *e = r.find(0x9218be09, 40000);
    CHECK(e && e->melody == 3 && e->transpose == -2 && e->tempo == 90 && e->name == "After shave_Macken.mid");
    CHECK(!r.find(0x9218be09, 40001));
    RegEntry n;
    n.crc = 0x9218be09, n.size = 40000, n.melody = 5, n.transpose = 1, n.tempo = 110, n.name = "x\ny";
    r.set(n);
    CHECK(r.size() == 2 && r.find(0x9218be09, 40000)->melody == 5);
    Registry r2;
    r2.load(r.save());
    CHECK(r2.size() == 2);
    const RegEntry *e2 = r2.find(0x9218be09, 40000);
    CHECK(e2 && e2->melody == 5 && e2->transpose == 1 && e2->tempo == 110 && e2->name == "x y");
    const RegEntry *a = r2.find(0x8c5c36ba, 51234);
    CHECK(a && a->melody == 2 && a->name == "Abba_Another town.mid");
    Registry empty;
    empty.load("");
    CHECK(empty.size() == 0 && empty.save()[0] == '#');
}

// v0.13.1: optional per-song SoundFont token, old lines must still load unchanged
static void testRegistrySf2()
{
    Registry r;
    r.load("# OnTheRoad karaoke registry: crc32 size melody(-1..15) transpose tempo% name\n"
           "8c5c36ba 51234 2 0 100 Abba_Another town.mid\n"                        // v0.13.0 line
           "9218be09 40000 3 -2 90 sf2=Florestan_Basic_GM_GS.sf2 After shave_Macken.mid\n"
           "11111111 100 -1 0 100 sf2=My%20Font%25.sf2 Song with spaces.kar\n"
           "22222222 200 1 0 100 sf2=../evil.sf2 Bad path.mid\n"
           "33333333 300 1 0 100 sf2=OnlyFont.sf2\n"
           "44444444 400 1 0 100 sf2=Odd%2.sf2 Odd escape.mid\n");
    CHECK(r.size() == 6);
    const RegEntry *a = r.find(0x8c5c36ba, 51234);
    CHECK(a && a->sf2.empty() && a->name == "Abba_Another town.mid" && a->melody == 2);
    const RegEntry *b = r.find(0x9218be09, 40000);
    CHECK(b && b->sf2 == "Florestan_Basic_GM_GS.sf2" && b->name == "After shave_Macken.mid" && b->transpose == -2 &&
          b->tempo == 90);
    const RegEntry *c = r.find(0x11111111, 100);
    CHECK(c && c->sf2 == "My Font%.sf2" && c->name == "Song with spaces.kar" && c->melody == -1);
    const RegEntry *d = r.find(0x22222222, 200);
    CHECK(d && d->sf2.empty() && d->name == "Bad path.mid");
    const RegEntry *e = r.find(0x33333333, 300);
    CHECK(e && e->sf2 == "OnlyFont.sf2" && e->name.empty());
    const RegEntry *f = r.find(0x44444444, 400);
    CHECK(f && f->sf2 == "Odd%2.sf2");
    // Round trip keeps the font, and lines without a font are written exactly as in v0.13.0
    const std::string text = r.save();
    CHECK(text.find("8c5c36ba 51234 2 0 100 Abba_Another town.mid\n") != std::string::npos);
    CHECK(text.find("sf2=My%20Font%25.sf2 Song with spaces.kar\n") != std::string::npos);
    Registry r2;
    r2.load(text);
    CHECK(r2.size() == 6);
    const RegEntry *c2 = r2.find(0x11111111, 100);
    CHECK(c2 && c2->sf2 == "My Font%.sf2" && c2->name == "Song with spaces.kar");
    const RegEntry *b2 = r2.find(0x9218be09, 40000);
    CHECK(b2 && b2->sf2 == "Florestan_Basic_GM_GS.sf2" && b2->name == "After shave_Macken.mid");
    // Clearing the font removes the token again
    RegEntry n = *b2;
    n.sf2.clear();
    r2.set(n);
    CHECK(r2.save().find("9218be09 40000 3 -2 90 After shave_Macken.mid\n") != std::string::npos);
}

static void testPlayer()
{
    std::vector<uint8_t> f = karFile();
    Song s;
    CHECK(parse(f.data(), f.size(), s) == Err::OK);
    Player p;
    Rec rec;
    p.advance(1000000, rec);
    CHECK(rec.msgs.empty());   // Nothing loaded
    p.load(&s);
    CHECK(!p.playing() && p.position() == 0);
    p.advance(1000000, rec);
    CHECK(rec.msgs.empty());   // Paused
    p.play(true);
    p.advance(0, rec);   // Events at t=0: program changes + CC
    size_t at0 = rec.msgs.size();
    CHECK(at0 == 3);
    p.advance(500000, rec);   // t = 0.5 s: first notes
    bool melOn = false;
    for (uint32_t m : rec.msgs)
        if (m == (0x93u << 16 | 60u << 8 | 100)) melOn = true;
    CHECK(melOn);
    CHECK(p.position() == 500000);
    // Tempo 50 %: 1 s real = 0.5 s song
    p.setTempo(50);
    p.advance(1000000, rec);
    CHECK(p.position() == 1000000);
    p.setTempo(10);
    CHECK(p.tempo() == 50);
    p.setTempo(500);
    CHECK(p.tempo() == 150);
    p.setTempo(100);
    // Seek back: sink reset + chase of programs/controllers only
    rec.msgs.clear();
    p.seek(250000, rec);
    CHECK(rec.resets == 1);
    CHECK(rec.msgs.size() == 3);
    for (uint32_t m : rec.msgs) CHECK(((m >> 16) & 0xF0) != 0x90);
    CHECK(p.position() == 250000);
    // Play to the end
    rec.msgs.clear();
    for (int i = 0; i < 100 && p.playing(); i++) p.advance(100000, rec);
    CHECK(p.finished() && !p.playing());
    size_t ons = 0, offs = 0;
    for (uint32_t m : rec.msgs) {
        uint8_t t = (m >> 16) & 0xF0;
        if (t == 0x90 && (m & 0xFF)) ons++;
        else if (t == 0x80 || t == 0x90) offs++;
    }
    CHECK(ons == 8 + 12 + 1 && offs == ons);
    p.seek(999999999, rec);
    CHECK(p.position() == s.lengthUs);
}

static void testRealFiles(int argc, char **argv)
{
    for (int i = 1; i < argc; i++) {
        FILE *fp = fopen(argv[i], "rb");
        if (!fp) continue;
        std::vector<uint8_t> d;
        uint8_t buf[4096];
        size_t n;
        while ((n = fread(buf, 1, sizeof buf, fp)) > 0) d.insert(d.end(), buf, buf + n);
        fclose(fp);
        Song s;
        Err e = parse(d.data(), d.size(), s);
        CHECK(e == Err::OK);
        CHECK(s.lines.size() > 5);
        CHECK(s.melody >= 0);
        std::string t, a;
        nameFromFile(argv[i], t, a);
        printf("  %-45.45s mel=%d lines=%zu len=%us\n", t.c_str(), s.melody, s.lines.size(),
               (unsigned)(s.lengthUs / 1000000));
    }
}

int main(int argc, char **argv)
{
    testParseKar();
    testParseLyricFormat0();
    testErrors();
    testUtf8AndNames();
    testRegistry();
    testRegistrySf2();
    testPlayer();
    testRealFiles(argc, argv);
    CHECK_DONE();
}
