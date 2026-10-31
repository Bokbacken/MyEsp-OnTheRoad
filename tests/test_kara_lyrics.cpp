// Host tests for bounded UTF-8 plain-text and LRC lyric sidecar parsing.
#include <string>

#include "check.h"
#include "kara_lyrics.h"

using namespace KaraLyrics;

int main()
{
    Document doc;
    const std::string plain = "\xEF\xBB\xBFH\xc3\xa9llo\r\n\nSecond line\n";
    CHECK(parse(plain.data(), plain.size(), doc) == Err::OK);
    CHECK(!doc.timed && doc.lines.size() == 3);
    CHECK(doc.lines[0].text == "H\xc3\xa9llo" && doc.lines[1].text.empty() && doc.lines[2].text == "Second line");

    const std::string lrc =
        "[ar:artist]\n[ti:title]\n[00:02.30]Second\n[00:01.25][00:03.50]First\n[00:01.25]First\n";
    CHECK(parse(lrc.data(), lrc.size(), doc) == Err::OK);
    CHECK(doc.timed && doc.lines.size() == 4);
    CHECK(doc.lines[0].us == 1250000 && doc.lines[0].text == "First");
    CHECK(doc.lines[1].us == 1250000 && doc.lines[1].text == "First");
    CHECK(doc.lines[2].us == 2300000 && doc.lines[2].text == "Second");
    CHECK(doc.lines[3].us == 3500000 && doc.lines[3].text == "First");

    const std::string precisions =
        "[00:01]whole\n[00:02.30]tenths\n[00:03.45]centis\n[00:04.567]millis\n";
    CHECK(parse(precisions.data(), precisions.size(), doc) == Err::OK);
    CHECK(doc.lines.size() == 4);
    CHECK(doc.lines[0].us == 1000000 && doc.lines[1].us == 2300000);
    CHECK(doc.lines[2].us == 3450000 && doc.lines[3].us == 4567000);

    const std::string blanks = "[00:01]One\n[00:02]\n[00:03]Three\n";
    CHECK(parse(blanks.data(), blanks.size(), doc) == Err::OK);
    CHECK(doc.lines.size() == 3 && doc.lines[1].text.empty() && doc.lines[1].us == 2000000);

    const std::string offsetNegative = "[offset:-250]\n[00:00.10]Zero\n[00:01]Later\n";
    CHECK(parse(offsetNegative.data(), offsetNegative.size(), doc) == Err::OK);
    CHECK(doc.lines[0].us == 0 && doc.lines[1].us == 750000);
    const std::string offsetPositive = "[00:01]One\n[offset:125]\n[00:02]Two\n";
    CHECK(parse(offsetPositive.data(), offsetPositive.size(), doc) == Err::OK);
    CHECK(doc.lines[0].us == 1125000 && doc.lines[1].us == 2125000);
    const std::string badOffset = "[offset:not-a-number]\n[00:01]Line\n";
    CHECK(parse(badOffset.data(), badOffset.size(), doc) == Err::BAD_LRC && doc.lines.empty());
    const std::string offsetOverflow = "[offset:4294968]\n[00:00]Line\n";
    CHECK(parse(offsetOverflow.data(), offsetOverflow.size(), doc) == Err::BAD_LRC);

    const std::string repeated =
        "[00:01]First\n[00:01]Second\n[00:01]\n[00:02][00:02]Repeated tag\n";
    CHECK(parse(repeated.data(), repeated.size(), doc) == Err::OK);
    CHECK(doc.lines.size() == 5);
    CHECK(doc.lines[0].us == 1000000 && doc.lines[0].text == "First");
    CHECK(doc.lines[1].us == 1000000 && doc.lines[1].text == "Second");
    CHECK(doc.lines[2].us == 1000000 && doc.lines[2].text.empty());
    CHECK(doc.lines[3].us == 2000000 && doc.lines[3].text == "Repeated tag");
    CHECK(doc.lines[4].us == 2000000 && doc.lines[4].text == "Repeated tag");

    const std::string malformed = "[01:90.50]bad";
    CHECK(parse(malformed.data(), malformed.size(), doc) == Err::BAD_LRC && doc.lines.empty());
    const std::string malformedConsecutive = "[00:01][01:90.50]bad";
    CHECK(parse(malformedConsecutive.data(), malformedConsecutive.size(), doc) == Err::BAD_LRC && doc.lines.empty());
    const std::string tooPrecise = "[00:01.1234]bad";
    CHECK(parse(tooPrecise.data(), tooPrecise.size(), doc) == Err::BAD_LRC);
    const std::string timeOverflow = "[100000:00]bad";
    CHECK(parse(timeOverflow.data(), timeOverflow.size(), doc) == Err::BAD_LRC);
    const char invalid[] = {'b', 'a', 'd', (char)0xC0, (char)0xAF};
    CHECK(parse(invalid, sizeof(invalid), doc) == Err::INVALID_UTF8);
    CHECK(parse("", 0, doc) == Err::EMPTY);

    std::string tooLong(MAX_LINE_BYTES + 1, 'x');
    CHECK(parse(tooLong.data(), tooLong.size(), doc) == Err::LINE_TOO_LONG);
    std::string tooBig(MAX_FILE + 1, 'x');
    CHECK(parse(tooBig.data(), tooBig.size(), doc) == Err::TOO_BIG);
    std::string many;
    for (size_t i = 0; i <= MAX_LINES; i++) many += "x\n";
    CHECK(parse(many.data(), many.size(), doc) == Err::TOO_MANY_LINES);

    CHECK_DONE();
}
