// Host test for main/fs_path.{h,cpp}
#include <string.h>

#include "check.h"
#include "fs_path.h"

static bool res(const char *user, const char *expect, const char *root = "/sdcard")
{
    char out[300];
    bool ok = FsPath::resolve(root, user, out, sizeof out);
    if (!expect) return !ok;
    if (!ok) { printf("  resolve(%s) failed\n", user); return false; }
    if (strcmp(out, expect) != 0) { printf("  resolve(%s) = %s\n", user, out); return false; }
    return true;
}

int main()
{
    CHECK(res("", "/sdcard"));
    CHECK(res(nullptr, "/sdcard"));
    CHECK(res("/", "/sdcard"));
    CHECK(res("otr/sounds/click.wav", "/sdcard/otr/sounds/click.wav"));
    CHECK(res("/otr//music/", "/sdcard/otr/music"));
    CHECK(res("/sdcard/otr", "/sdcard/otr"));
    CHECK(res("/sdcard", "/sdcard"));
    CHECK(res("/sdcardx/a", "/sdcard/sdcardx/a"));
    CHECK(res("./a/./b", "/sdcard/a/b"));
    CHECK(res("Mapp med mellanslag/fil.txt", "/sdcard/Mapp med mellanslag/fil.txt"));
    CHECK(res("\xc3\xa5\xc3\xa4\xc3\xb6.txt", "/sdcard/\xc3\xa5\xc3\xa4\xc3\xb6.txt"));   // UTF-8 ok
    char reported[90];
    memset(reported, 'x', 87);
    reported[87] = 0;
    CHECK(FsPath::validName(reported));
    memset(reported, 'y', 81);
    reported[81] = 0;
    CHECK(FsPath::validName(reported));
    CHECK(res("a", "/ffat/a", "/ffat"));
    CHECK(res("/ffat/x/y", "/ffat/x/y", "/ffat"));
    // Rejected
    CHECK(res("..", nullptr));
    CHECK(res("../etc", nullptr));
    CHECK(res("a/../../b", nullptr));
    CHECK(res("a/..", nullptr));
    CHECK(res("/sdcard/../ffat", nullptr));
    CHECK(res("a\\b", nullptr));
    CHECK(res("a:b", nullptr));
    CHECK(res("a*b", nullptr));
    CHECK(res("a?b", nullptr));
    CHECK(res("a\x01" "b", nullptr));
    CHECK(res("a|b", nullptr));
    char longc[300];
    memset(longc, 'x', 256);
    longc[256] = 0;
    CHECK(res(longc, nullptr));
    memset(longc, 'x', 255);
    longc[255] = 0;
    CHECK(FsPath::validName(longc));
    longc[255] = 'x';
    longc[256] = 0;
    CHECK(!FsPath::validName(longc));
    char small[12];
    CHECK(!FsPath::resolve("/sdcard", "abcdef", small, sizeof small));
    CHECK(FsPath::resolve("/sdcard", "abc", small, sizeof small));
    CHECK(!FsPath::resolve("sdcard", "a", small, sizeof small));
    CHECK(!FsPath::resolve(nullptr, "a", small, sizeof small));
    // FAT silently strips trailing dots, so paths and single-name operations agree.
    CHECK(res("...", nullptr));

    CHECK(FsPath::validName("save.sav"));
    CHECK(FsPath::validName("Ny mapp"));
    // FAT counts UTF-16 code units, not UTF-8 bytes.
    char unicode[516];
    for (size_t i = 0; i < 127; i++) {
        unicode[i * 4] = (char)0xF0;
        unicode[i * 4 + 1] = (char)0x9F;
        unicode[i * 4 + 2] = (char)0x98;
        unicode[i * 4 + 3] = (char)0x80;
    }
    unicode[127 * 4] = 0;
    CHECK(FsPath::validName(unicode));
    char resolvedUnicode[300];
    CHECK(!FsPath::resolve("/sdcard", unicode, resolvedUnicode, sizeof(resolvedUnicode)));
    unicode[127 * 4] = (char)0xF0;
    unicode[127 * 4 + 1] = (char)0x9F;
    unicode[127 * 4 + 2] = (char)0x98;
    unicode[127 * 4 + 3] = (char)0x80;
    unicode[128 * 4] = 0;
    CHECK(!FsPath::validName(unicode));
    CHECK(!FsPath::validName("\xC0\xAF"));
    CHECK(!FsPath::validName("\xED\xA0\x80"));
    CHECK(!FsPath::validName("\xC2\x80"));
    CHECK(!FsPath::validName(""));
    CHECK(!FsPath::validName(nullptr));
    CHECK(!FsPath::validName("."));
    CHECK(!FsPath::validName(".."));
    CHECK(!FsPath::validName("a/b"));
    CHECK(!FsPath::validName("a\\b"));
    CHECK(!FsPath::validName("trailing."));
    CHECK(!FsPath::validName("trailing "));
    CHECK(!FsPath::validName(longc));

    CHECK(FsPath::isRoot("/sdcard", "/sdcard"));
    CHECK(FsPath::isRoot("/sdcard", "/sdcard/"));
    CHECK(!FsPath::isRoot("/sdcard", "/sdcard/a"));
    CHECK(!FsPath::isRoot("/sdcard", "/ffat"));
    CHECK_DONE();
}
