#include "supernova_lyrics.h"

#include <assert.h>
#include <string.h>

using namespace SupernovaLyrics;

int main()
{
    const char song[] =
        "\xEF\xBB\xBF#VERSION:1.0.0\r\n"
        "#TITLE:Stjärnor\r\n"
        "#ARTIST:Demo\r\n"
        "#BPM:120\r\n"
        "#GAP:250\r\n"
        ": 0 4 60 Stjärn\r\n"
        "* 4 2 62 or\r\n"
        "R 6 2 63  text\r\n"
        "X 8 2 64 extra\r\n"
        "- 10\r\n"
        "F 0 4 64 Nästa\r\n"
        "- 4\r\n"
        "E\r\n"
        ": 0 4 60 ignored\r\n";

    Document doc;
    assert(parse(song, sizeof(song) - 1, doc) == Err::OK);
    assert(doc.title == "Stjärnor");
    assert(doc.artist == "Demo");
    assert(doc.bpm == 120);
    assert(doc.gapMs == 250);
    assert(doc.phrases.size() == 2);
    assert(doc.phrases[0].text == "Stjärnor textextra");
    assert(doc.phrases[0].notes.size() == 4);
    assert(doc.phrases[0].notes[2].text == " text");
    assert(doc.phrases[0].notes[0].startMs == 250);
    assert(doc.phrases[0].notes[1].startMs == 750);
    assert(doc.phrases[0].notes[1].kind == NoteKind::GOLDEN);
    assert(doc.phrases[0].notes[2].kind == NoteKind::RAP);
    assert(doc.phrases[0].notes[3].kind == NoteKind::FREESTYLE);
    assert(doc.phrases[1].notes[0].kind == NoteKind::FREESTYLE);
    assert(phraseAt(doc, 251) == 0);
    assert(phraseAt(doc, 1550) == 1);
    assert(noteAt(doc.phrases[0], 751) == 1);

    const char relative[] =
        "#BPM:60,0\n#GAP:0\n#RELATIVE:yes\n"
        ": 0 4 60 A\n- 4\n"
        ": 0 4 62 B\n- 4\nE\n";
    assert(parse(relative, sizeof(relative) - 1, doc) == Err::OK);
    assert(doc.relative);
    assert(doc.phrases.size() == 2);
    assert(doc.phrases[1].notes[0].startMs == 1000);

    const char badBpm[] = "#TITLE:x\n: 0 4 60 hi\nE\n";
    assert(parse(badBpm, sizeof(badBpm) - 1, doc) == Err::BAD_HEADER);
    const char unsupported[] = "#VERSION:2.0.0\n#BPM:100\n: 0 4 60 hi\nE\n";
    assert(parse(unsupported, sizeof(unsupported) - 1, doc) == Err::UNSUPPORTED_VERSION);
    const char badUtf8[] = {'#', 'B', 'P', 'M', ':', '1', '0', '0', '\n', (char)0xFF};
    assert(parse(badUtf8, sizeof(badUtf8), doc) == Err::INVALID_UTF8);
    assert(parse(nullptr, 0, doc) == Err::EMPTY);
    return 0;
}
