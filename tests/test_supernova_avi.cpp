// Host test for SupernovaAvi: builds a tiny AVI (MJPEG video + MP3 audio, ffmpeg-style idx1) in memory.
#include <stdio.h>
#include <string.h>

#include <string>
#include <vector>

#include "supernova_avi.h"

#define CHECK(cond)                                                      \
    do {                                                                 \
        if (!(cond)) {                                                   \
            fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, #cond); \
            return 1;                                                    \
        }                                                                \
    } while (0)

using Bytes = std::vector<uint8_t>;

static void put32(Bytes &b, uint32_t v) { for (int i = 0; i < 4; i++) b.push_back((uint8_t)(v >> (8 * i))); }
static void put16(Bytes &b, uint16_t v) { b.push_back((uint8_t)v); b.push_back((uint8_t)(v >> 8)); }
static void putTag(Bytes &b, const char *t) { b.insert(b.end(), t, t + 4); }
static void chunk(Bytes &b, const char *id, const Bytes &data)
{
    putTag(b, id);
    put32(b, (uint32_t)data.size());
    b.insert(b.end(), data.begin(), data.end());
    if (data.size() & 1) b.push_back(0);
}

static Bytes strl(bool video)
{
    Bytes strh, strf;
    putTag(strh, video ? "vids" : "auds");
    putTag(strh, video ? "MJPG" : "\0\0\0\0");
    for (int i = 0; i < 3; i++) put32(strh, 0);   // flags, priority/language, initialFrames
    put32(strh, 1);                               // scale
    put32(strh, video ? 10 : 38);                 // rate
    for (int i = 0; i < 6; i++) put32(strh, 0);
    if (video) {
        put32(strf, 40); put32(strf, 64); put32(strf, 48); put16(strf, 1); put16(strf, 24);
        putTag(strf, "MJPG");
        for (int i = 0; i < 5; i++) put32(strf, 0);
    } else {
        put16(strf, 0x55); put16(strf, 2); put32(strf, 44100); put32(strf, 16000); put16(strf, 1); put16(strf, 0); put16(strf, 0);
    }
    Bytes list;
    putTag(list, "strl");
    chunk(list, "strh", strh);
    chunk(list, "strf", strf);
    return list;
}

int main()
{
    Bytes avih(56, 0);
    avih[0] = 0xA0; avih[1] = 0x86; avih[2] = 0x01;   // 100000 us = 10 fps
    avih[32] = 64; avih[36] = 48;
    Bytes hdrl;
    putTag(hdrl, "hdrl");
    chunk(hdrl, "avih", avih);
    chunk(hdrl, "LIST", strl(true));
    chunk(hdrl, "LIST", strl(false));

    Bytes movi;
    putTag(movi, "movi");
    struct E { std::string id; uint32_t off, size; };
    std::vector<E> idx;
    Bytes expectAudio;
    auto add = [&](const char *id, const Bytes &data) {
        idx.push_back({id, (uint32_t)movi.size(), (uint32_t)data.size()});   // Relative to the "movi" fourcc
        chunk(movi, id, data);
    };
    for (int f = 0; f < 3; f++) {
        add("00dc", Bytes(7 + f, (uint8_t)(0xA0 + f)));
        for (int a = 0; a < 2; a++) {
            Bytes mp3(5 + a, (uint8_t)(f * 2 + a));
            expectAudio.insert(expectAudio.end(), mp3.begin(), mp3.end());
            add("01wb", mp3);
        }
    }
    Bytes idx1;
    for (const E &e : idx) { putTag(idx1, e.id.c_str()); put32(idx1, 0x10); put32(idx1, e.off); put32(idx1, e.size); }

    Bytes body;
    putTag(body, "AVI ");
    chunk(body, "LIST", hdrl);
    chunk(body, "LIST", movi);
    chunk(body, "idx1", idx1);
    Bytes file;
    putTag(file, "RIFF");
    put32(file, (uint32_t)body.size());
    file.insert(file.end(), body.begin(), body.end());

    SupernovaAvi::ReadFn read = [&](uint32_t off, void *buf, size_t n) {
        if ((uint64_t)off + n > file.size()) return false;
        memcpy(buf, file.data() + off, n);
        return true;
    };
    SupernovaAvi::Info info;
    CHECK(SupernovaAvi::parse(read, (uint32_t)file.size(), info));
    CHECK(info.width == 64 && info.height == 48);
    CHECK(info.fps > 9.99 && info.fps < 10.01);
    CHECK(info.video.size() == 3 && info.audio.size() == 6);
    CHECK(info.video[1].size == 8 && file[info.video[1].off] == 0xA1);
    CHECK(info.audioBytes() == expectAudio.size());
    CHECK(info.durationMs() == 300);

    Bytes got(expectAudio.size());
    CHECK(SupernovaAvi::readAudio(info, read, 0, got.data(), got.size()) == got.size() && got == expectAudio);
    Bytes part(9);   // Starts and ends in the middle of chunks
    CHECK(SupernovaAvi::readAudio(info, read, 7, part.data(), part.size()) == part.size());
    CHECK(part == Bytes(expectAudio.begin() + 7, expectAudio.begin() + 16));
    CHECK(SupernovaAvi::readAudio(info, read, (uint32_t)expectAudio.size(), part.data(), part.size()) == 0);

    Bytes bad = file;
    bad[8] = 'X';
    SupernovaAvi::ReadFn readBad = [&](uint32_t off, void *buf, size_t n) {
        if ((uint64_t)off + n > bad.size()) return false;
        memcpy(buf, bad.data() + off, n);
        return true;
    };
    CHECK(!SupernovaAvi::parse(readBad, (uint32_t)bad.size(), info));
    puts("OK");
    return 0;
}
