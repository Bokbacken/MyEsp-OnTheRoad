// Host test for main/otr_log_core.h (v0.13.7 log ring + store ZIP)
#include <stdio.h>
#include <string.h>

#include "check.h"
#include "otr_log_core.h"

using namespace OtrLogCore;

static uint32_t rd32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }

int main()
{
    // Ring basics
    char buf[16];
    Ring r;
    char out[64];
    uint64_t from = 0;
    CHECK(r.read(out, sizeof out, &from) == 0);   // Not attached
    r.attach(buf, sizeof buf);
    CHECK(r.head() == 0 && r.oldest() == 0);
    r.write("hello ", 6);
    CHECK(r.read(out, sizeof out, &from) == 6 && !memcmp(out, "hello ", 6) && from == 6);
    CHECK(r.read(out, sizeof out, &from) == 0);
    r.write("world 123456", 12);   // head 18: wraps, oldest 2
    CHECK(r.head() == 18 && r.oldest() == 2);
    CHECK(r.read(out, sizeof out, &from) == 12 && !memcmp(out, "world 123456", 12) && from == 18);
    uint64_t f0 = 0;   // Reader that fell behind jumps to the oldest byte
    CHECK(r.read(out, sizeof out, &f0) == 16 && f0 == 18 && !memcmp(out, "llo world 123456", 16));
    uint64_t f1 = 3;
    CHECK(r.read(out, 4, &f1) == 4 && f1 == 7 && !memcmp(out, "lo w", 4));
    uint64_t f2 = 999;   // Beyond the head: nothing
    CHECK(r.read(out, sizeof out, &f2) == 0 && f2 == 18);
    r.write("ABCDEFGHIJKLMNOPQRSTUVWXYZ", 26);   // Larger than the ring: only the tail stays
    uint64_t f3 = 0;
    CHECK(r.head() == 44 && r.read(out, sizeof out, &f3) == 16 && !memcmp(out, "KLMNOPQRSTUVWXYZ", 16));
    // Many small writes keep the order
    Ring r2;
    char big[1000];
    r2.attach(big, sizeof big);
    std::string all;
    for (int i = 0; i < 500; i++) {
        char line[32];
        int n = snprintf(line, sizeof line, "I (%d) t: line %d\n", i, i);
        r2.write(line, (size_t)n);
        all.append(line, (size_t)n);
    }
    std::string got;
    uint64_t f4 = 0;
    char chunk[77];
    size_t n;
    while ((n = r2.read(chunk, sizeof chunk, &f4)) > 0) got.append(chunk, n);
    CHECK(got.size() == 1000 && got == all.substr(all.size() - 1000));

    // CRC-32 (standard check value)
    {   // Line cleaning + level
        char o[64];
        const char *a = "\033[0;32mI (123) wifi: up\033[0m\n";
        size_t n = cleanLine(a, strlen(a), o);
        CHECK(n == 17 && std::string(o, n) == "I (123) wifi: up\n");
        CHECK(lineLevel(o, n) == 'I');
        CHECK(cleanLine("\n\r\n", 3, o) == 0);
        CHECK(cleanLine("abc", 3, o) == 4 && std::string(o, 4) == "abc\n");
        CHECK(cleanLine("x\r\n\n", 4, o) == 2 && std::string(o, 2) == "x\n");
        CHECK(cleanLine("\033[0;31m", 7, o) == 0);
        CHECK(lineLevel("E (1) a", 7) == 'E' && lineLevel("W (1)", 5) == 'W');
        CHECK(lineLevel("hello", 5) == '?' && lineLevel("X (1)", 5) == '?' && lineLevel("E", 1) == '?');
    }
    CHECK(crc32Update(0, "123456789", 9) == 0xCBF43926u);
    CHECK(crc32Update(crc32Update(0, "1234", 4), "56789", 5) == 0xCBF43926u);
    CHECK(crc32Update(0, "", 0) == 0);

    // ZIP: two entries, then parse it back
    const char *a = "first file\n", *b = "second\nfile\n";
    std::vector<ZipEntry> es;
    std::vector<uint8_t> zip, hdr;
    const uint16_t t = dosTime(13, 45, 30), d = dosDate(2026, 10, 25);
    for (const char *data : {a, b}) {
        ZipEntry e{data == a ? "a.log" : "logs_b.log", crc32Update(0, data, strlen(data)), (uint32_t)strlen(data),
                   (uint32_t)zip.size()};
        zipLocal(hdr, e, t, d);
        CHECK(hdr.size() == zipLocalSize(e.name));
        zip.insert(zip.end(), hdr.begin(), hdr.end());
        zip.insert(zip.end(), data, data + strlen(data));
        es.push_back(e);
    }
    const uint32_t cd = (uint32_t)zip.size();
    zipDirectory(hdr, es, cd, t, d);
    zip.insert(zip.end(), hdr.begin(), hdr.end());
    const uint8_t *end = zip.data() + zip.size() - 22;
    CHECK(rd32(end) == 0x06054b50 && rd16(end + 10) == 2 && rd32(end + 16) == cd);
    CHECK(rd32(end + 12) == zip.size() - 22 - cd);
    const uint8_t *c = zip.data() + cd;
    CHECK(rd32(c) == 0x02014b50 && rd16(c + 10) == 0 && rd32(c + 42) == 0 && rd16(c + 28) == 5);
    const uint8_t *c2 = c + 46 + 5;
    CHECK(rd32(c2) == 0x02014b50 && rd32(c2 + 42) == es[1].offset && !memcmp(c2 + 46, "logs_b.log", 10));
    const uint8_t *l2 = zip.data() + es[1].offset;
    CHECK(rd32(l2) == 0x04034b50 && rd32(l2 + 14) == es[1].crc && rd32(l2 + 18) == strlen(b));
    CHECK(!memcmp(l2 + 30 + 10, b, strlen(b)));
    CHECK(dosDate(2026, 10, 25) == (uint16_t)((46 << 9) | (10 << 5) | 25));
    CHECK(dosDate(1970, 1, 1) == 0x21);
    CHECK(dosTime(23, 59, 59) == (uint16_t)((23 << 11) | (59 << 5) | 29));
    // Write it for a manual check with unzip -t
    if (FILE *f = fopen("/home/ubuntu/.tmp/t/test_otrlog.zip", "wb")) {
        fwrite(zip.data(), 1, zip.size(), f);
        fclose(f);
    }
    CHECK_DONE();
}
