// Host tests for the pure-logic headers: net_list.h (saved Wi-Fi networks) and
// mypi_kbd_core.h (scroll-wheel keyboard).
//   g++ -std=c++17 -Wall -Wextra -I MyEsp_OnTheRoad tests/test_core.cpp -o tests/test_core && tests/test_core
#include <cstdio>
#include <cstring>

#include "mypi_kbd_core.h"
#include "net_list.h"
#include "player_db.h"
#include "roam_policy.h"

static int g_fail = 0, g_pass = 0;
#define CHECK(cond)                                                              \
    do {                                                                         \
        if (cond) g_pass++;                                                      \
        else { g_fail++; std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); } \
    } while (0)
#define CHECK_STR(a, b) CHECK(std::strcmp((a), (b)) == 0)

static void testNetList()
{
    static NetList l;
    l.clear();
    CHECK(l.count == 0);
    CHECK(l.upsert("", "x") == -1);
    CHECK(l.upsert("123456789012345678901234567890123", "") == -1);   // 33 chars
    CHECK(l.upsert("A", "passwordA") == 0);
    CHECK(l.upsert("B", "passwordB") == 0);   // New goes on top
    CHECK(l.upsert("C", nullptr) == 0);
    CHECK(l.count == 3);
    CHECK_STR(l.items[0].ssid, "C");
    CHECK_STR(l.items[2].ssid, "A");
    CHECK_STR(l.items[0].pass, "");
    // Update keeps position
    CHECK(l.upsert("A", "newpass1") == 2);
    CHECK_STR(l.items[2].pass, "newpass1");
    CHECK(l.count == 3);
    // move
    CHECK(l.move(2, -1));   // A up: C A B
    CHECK_STR(l.items[1].ssid, "A");
    CHECK_STR(l.items[2].ssid, "B");
    CHECK(l.move(0, 10));   // C to bottom (clamped): A B C
    CHECK_STR(l.items[0].ssid, "A");
    CHECK_STR(l.items[2].ssid, "C");
    CHECK(!l.move(0, -1));  // Already top
    CHECK(!l.move(5, 1));
    // firstVisible
    const char *vis[] = {"X", "C", "B"};
    CHECK(l.firstVisible(vis, 3) == 1);       // B (index 1) before C (index 2)
    CHECK(l.firstVisible(vis, 3, 2) == 2);    // Next after a failed B
    CHECK(l.firstVisible(vis, 3, 3) == -1);
    const char *none[] = {"Z"};
    CHECK(l.firstVisible(none, 1) == -1);
    // remove
    CHECK(l.remove(1));     // A C
    CHECK(l.count == 2);
    CHECK_STR(l.items[1].ssid, "C");
    CHECK(l.items[2].ssid[0] == 0);
    CHECK(!l.remove(2));
    CHECK(l.find("C") == 1 && l.find("B") == -1 && l.find(nullptr) == -1);
    // Full list: newest on top, last one drops
    l.clear();
    char name[8];
    for (int i = 0; i < NET_LIST_MAX; i++) { std::snprintf(name, sizeof(name), "n%d", i); l.upsert(name, ""); }
    CHECK(l.count == NET_LIST_MAX);
    CHECK_STR(l.items[0].ssid, "n49");
    CHECK_STR(l.items[NET_LIST_MAX - 1].ssid, "n0");
    CHECK(l.upsert("new", "") == 0);
    CHECK(l.count == NET_LIST_MAX);
    CHECK_STR(l.items[0].ssid, "new");
    CHECK_STR(l.items[NET_LIST_MAX - 1].ssid, "n1");   // n0 dropped
    CHECK(l.find("n0") == -1);
    // Password length limit (64 = raw PSK hex allowed, 65 rejected)
    char p64[65], p65[66];
    std::memset(p64, 'a', 64); p64[64] = 0;
    std::memset(p65, 'a', 65); p65[65] = 0;
    CHECK(l.upsert("new", p64) == 0);
    CHECK(l.upsert("new", p65) == -1);
}

static bool noLatinUpper(uint32_t cp, void *) { return !(cp >= 0xC0 && cp <= 0xDE); }
static bool onlyAscii(uint32_t cp, void *) { return cp < 0x80; }

static void testKbd()
{
    mypi_kbd_core_t k;
    mypi_kbd_core_init(&k, "ab", 4, nullptr, nullptr);
    CHECK_STR(k.text, "ab");
    CHECK(k.range == MYPI_KBD_RANGE_LOWER);
    CHECK(mypi_kbd_current(&k, 0) == 'a');
    CHECK(mypi_kbd_current(&k, -1) == 'z');   // Wraps
    mypi_kbd_step(&k, 3);
    CHECK(mypi_kbd_current(&k, 0) == 'd');
    CHECK(mypi_kbd_insert(&k));
    CHECK_STR(k.text, "abd");
    CHECK(mypi_kbd_insert(&k));
    CHECK(!mypi_kbd_insert(&k));              // max_len 4
    CHECK_STR(k.text, "abdd");
    CHECK(mypi_kbd_delete_char(&k));
    CHECK_STR(k.text, "abd");
    // Ranges
    mypi_kbd_next_range(&k, 1);
    CHECK(k.range == MYPI_KBD_RANGE_UPPER && k.index == 0);
    mypi_kbd_next_range(&k, -1);
    mypi_kbd_next_range(&k, -1);
    CHECK(k.range == MYPI_KBD_RANGE_LATIN);
    CHECK(mypi_kbd_current(&k, 0) == 0xE5);   // å
    // Multi-byte insert/delete
    mypi_kbd_core_t u;
    mypi_kbd_core_init(&u, "", 10, nullptr, nullptr);
    mypi_kbd_set_range(&u, MYPI_KBD_RANGE_LATIN);
    CHECK(mypi_kbd_insert(&u));
    CHECK_STR(u.text, "\xC3\xA5");
    CHECK(mypi_kbd_char_count(u.text) == 1);
    CHECK(mypi_kbd_delete_char(&u));
    CHECK_STR(u.text, "");
    CHECK(!mypi_kbd_delete_char(&u));
    // Truncation never splits UTF-8
    mypi_kbd_core_init(&u, "a\xC3\xA5\xC3\xA4", 4, nullptr, nullptr);
    CHECK_STR(u.text, "a\xC3\xA5");
    // Glyph filter
    mypi_kbd_core_t f;
    mypi_kbd_core_init(&f, "", 64, noLatinUpper, nullptr);
    CHECK(mypi_kbd_range_count(&f, MYPI_KBD_RANGE_LATIN) < MYPI_KBD_LATIN_COUNT);
    mypi_kbd_core_init(&f, "", 64, onlyAscii, nullptr);
    CHECK(mypi_kbd_range_count(&f, MYPI_KBD_RANGE_LATIN) == 0);
    CHECK(!mypi_kbd_set_range(&f, MYPI_KBD_RANGE_LATIN));
    f.range = MYPI_KBD_RANGE_SYMBOL;
    mypi_kbd_next_range(&f, 1);               // Skips the empty Latin range
    CHECK(f.range == MYPI_KBD_RANGE_LOWER);
    CHECK(MYPI_KBD_SYMBOL_COUNT == 33);
}

static void testPlayers()
{
    static PlayerDb d;
    d.clear();
    CHECK(d.add("  Kent ", 100) == 1);
    CHECK_STR(d.items[0].name, "Kent");
    CHECK(d.items[0].created == 100);
    CHECK(d.add("kent", 0) == (int)PlayerErr::DUPLICATE);
    CHECK(d.add("KENT", 0) == (int)PlayerErr::DUPLICATE);
    CHECK(d.add("", 0) == (int)PlayerErr::INVALID);
    CHECK(d.add("   ", 0) == (int)PlayerErr::INVALID);
    CHECK(d.add("12345678901234567890123", 0) > 0);        // 23 bytes ok
    CHECK(d.add("123456789012345678901234", 0) == (int)PlayerErr::INVALID);
    CHECK(d.add("a\"b", 0) == (int)PlayerErr::INVALID);
    CHECK(d.add("\xC3\x85sa", 0) == 3);                     // Åsa
    CHECK(d.add("\xC3\xA5SA", 0) == (int)PlayerErr::DUPLICATE);   // åSA
    CHECK(d.add("\xC3\x96rjan", 0) == 4);                   // Örjan
    CHECK(d.add("Bo", 0) == 5);
    CHECK(d.add("anna", 0) == 6);
    // Sorted: anna, Bo, Kent, 123..., Åsa, Örjan  (digits first)
    uint8_t idx[PLAYER_MAX];
    int n = d.sortedIndices(idx);
    CHECK(n == 6);
    CHECK_STR(d.items[idx[0]].name, "12345678901234567890123");
    CHECK_STR(d.items[idx[1]].name, "anna");
    CHECK_STR(d.items[idx[2]].name, "Bo");
    CHECK_STR(d.items[idx[3]].name, "Kent");
    CHECK_STR(d.items[idx[4]].name, "\xC3\x85sa");
    CHECK_STR(d.items[idx[5]].name, "\xC3\x96rjan");
    // rename
    CHECK(d.rename(5, "bo") == PlayerErr::OK);              // Same player, case change ok
    CHECK_STR(d.get(5)->name, "bo");
    CHECK(d.rename(5, "Kent") == PlayerErr::DUPLICATE);
    CHECK(d.rename(99, "X") == PlayerErr::NOT_FOUND);
    // remove: ids are not reused
    CHECK(d.remove(6) == PlayerErr::OK);
    CHECK(d.remove(6) == PlayerErr::NOT_FOUND);
    CHECK(d.add("Nisse", 0) == 7);
    CHECK(d.indexOfName(" nisse ") >= 0);
    CHECK(d.get(1) && !d.get(6) && !d.get(0));
    // Full
    d.clear();
    char nm[16];
    for (int i = 0; i < PLAYER_MAX; i++) { std::snprintf(nm, sizeof(nm), "P%d", i); CHECK(d.add(nm, 0) == i + 1); }
    CHECK(d.add("extra", 0) == (int)PlayerErr::FULL);
    CHECK(d.count == PLAYER_MAX);
    // Id wrap skips taken ids
    d.clear();
    d.add("A", 0);            // id 1
    d.nextId = 65535;
    CHECK(d.add("B", 0) == 65535);
    CHECK(d.add("C", 0) == 2); // 1 is taken
    CHECK(sizeof(PlayerRec) == 40);
}

static void testPlayerPin()
{
    CHECK(PlayerDb::validPin("1234"));
    CHECK(PlayerDb::validPin("0000"));
    CHECK(!PlayerDb::validPin("123"));
    CHECK(!PlayerDb::validPin("12345"));
    CHECK(!PlayerDb::validPin("12a4"));
    CHECK(!PlayerDb::validPin(" 123"));
    CHECK(!PlayerDb::validPin(""));
    CHECK(PlayerDb::validPin("", true));
    CHECK(PlayerDb::validPin(nullptr, true));
    CHECK(!PlayerDb::validPin(nullptr));
    static PlayerDb d;
    d.clear();
    CHECK(d.add("Kent", 0, "12x4") == (int)PlayerErr::BAD_PIN);
    CHECK(d.count == 0);
    CHECK(d.add("Kent", 0, "4321") == 1);
    CHECK(d.add("Bo", 0) == 2);
    CHECK(d.add("Ia", 0, "") == 3);
    CHECK(d.hasPin(1) && !d.hasPin(2) && !d.hasPin(3) && !d.hasPin(99));
    CHECK(d.checkPin(1, "4321"));
    CHECK(!d.checkPin(1, "4320"));
    CHECK(!d.checkPin(1, "43210"));
    CHECK(!d.checkPin(1, ""));
    CHECK(!d.checkPin(2, ""));        // No PIN never matches
    CHECK(!d.checkPin(2, "0000"));
    CHECK(d.setPin(2, "99") == PlayerErr::BAD_PIN);
    CHECK(d.setPin(2, "0099") == PlayerErr::OK && d.checkPin(2, "0099"));
    CHECK(d.setPin(2, "") == PlayerErr::OK && !d.hasPin(2));
    CHECK(d.setPin(77, "1111") == PlayerErr::NOT_FOUND);
    // rename keeps the PIN
    CHECK(d.rename(1, "Kenta") == PlayerErr::OK && d.checkPin(1, "4321"));
    // sanitize drops garbage PINs
    std::memcpy(d.items[2].pin, "ab", 3);
    d.sanitize();
    CHECK(!d.hasPin(3) && d.hasPin(1));
}

static void testPlayerMigration()
{
    static PlayerDbV1 old;
    std::memset(&old, 0, sizeof(old));
    old.nextId = 9;
    old.count = 2;
    old.items[0] = {3, 0, 1700000000u, "Kent"};
    old.items[1].id = 8;
    std::memset(old.items[1].name, 'x', PLAYER_NAME_CAP);   // Unterminated: must be cut
    static PlayerDb d;
    CHECK(d.loadV1(old));
    CHECK(d.count == 2 && d.nextId == 9);
    CHECK(d.items[0].id == 3 && d.items[0].created == 1700000000u);
    CHECK_STR(d.items[0].name, "Kent");
    CHECK(std::strlen(d.items[1].name) == PLAYER_NAME_CAP - 1);
    CHECK(!d.hasPin(3) && !d.hasPin(8));
    CHECK(d.add("Ny", 0) == 9);
    old.count = PLAYER_MAX + 1;
    CHECK(!d.loadV1(old));
}

static void testRoam()
{
    using namespace Roam;
    // Kent's case: connected to a distant AP at -80 dBm while a -48 dBm AP is visible.
    Ap aps[4] = {
        {{1, 1, 1, 1, 1, 1}, -80, 1},    // current (far)
        {{2, 2, 2, 2, 2, 2}, -48, 6},
        {{3, 3, 3, 3, 3, 3}, -66, 11},
        {{4, 4, 4, 4, 4, 4}, -71, 1},
    };
    const uint8_t cur[6] = {1, 1, 1, 1, 1, 1};
    CHECK(pickTarget(aps, 4, cur, -80) == 1);
    CHECK(strongest(aps, 4) == 1);
    CHECK(strongest(aps, 0) == -1);
    // Scan measurement of the current AP wins over the link RSSI.
    CHECK(pickTarget(aps, 4, cur, -40) == 1);
    // Good link: never roam.
    CHECK(!shouldRoam(-55, -30));
    CHECK(!shouldRoam(-50, -30));
    // Hysteresis: 8 dB normally, 5 dB on a weak link.
    CHECK(!shouldRoam(-65, -58));
    CHECK(shouldRoam(-65, -57));
    CHECK(!shouldRoam(-78, -74));
    CHECK(shouldRoam(-78, -73));
    // Only the current AP visible -> no target; current AP strongest -> no target.
    CHECK(pickTarget(aps, 1, cur, -80) == -1);
    Ap one[2] = {{{1, 1, 1, 1, 1, 1}, -60, 1}, {{2, 2, 2, 2, 2, 2}, -63, 6}};
    CHECK(pickTarget(one, 2, cur, -60) == -1);
    // Current BSSID not in scan: link RSSI is used.
    const uint8_t gone[6] = {9, 9, 9, 9, 9, 9};
    CHECK(pickTarget(one, 2, gone, -75) == 0);
    CHECK(pickTarget(one, 2, gone, -64) == -1);
    // Scan interval shrinks with weaker links.
    CHECK(scanIntervalMs(-50) == 300000);
    CHECK(scanIntervalMs(-60) == 60000);
    CHECK(scanIntervalMs(-70) == 30000);
    CHECK(scanIntervalMs(-85) == 15000);
    CHECK(scanIntervalMs(-100) <= scanIntervalMs(-40));
    char b[18];
    const uint8_t mac[6] = {0x28, 0x84, 0x85, 0x0A, 0xFF, 0x00};
    formatBssid(mac, b);
    CHECK_STR(b, "28:84:85:0A:FF:00");
}

int main()
{
    testRoam();
    testPlayers();
    testPlayerPin();
    testPlayerMigration();
    testNetList();
    testKbd();
    std::printf("%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
