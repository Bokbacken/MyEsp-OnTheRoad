#include "adv_validator.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>

namespace {

int checks;
#define CHECK(condition) do { \
    ++checks; \
    if (!(condition)) { \
        std::fprintf(stderr, "FAIL %d: %s\n", __LINE__, #condition); \
        std::abort(); \
    } \
} while (0)

void accepts(const std::string &text)
{
    Adv::ValidationResult result{};
    CHECK(Adv::validateText(text.data(), text.size(), result));
    if (!result.valid) std::fprintf(stderr, "unexpected validation error: %s\n", result.message);
    CHECK(result.valid);
}

void rejects(const std::string &text, int mapId = 0)
{
    Adv::ValidationResult result{};
    CHECK(Adv::validateText(text.data(), text.size(), result));
    CHECK(!result.valid);
    if (result.errorMapId != mapId)
        std::fprintf(stderr, "expected error on map %d, received map %d: %s\n",
                     mapId, result.errorMapId, result.message);
    CHECK(result.errorMapId == mapId);
    CHECK(result.message[0] != 0);
}

void testCampaign()
{
    const char *files[] = {
        "sdcard_bundle/otr/adventure/motoraventyr/01_motvind/01_motvind.adv",
        "sdcard_bundle/otr/adventure/motoraventyr/02_stenporten/01_stenporten.adv",
        "sdcard_bundle/otr/adventure/motoraventyr/03_fyrarnas_sang/01_fyrarnas_sang.adv",
        "sdcard_bundle/otr/adventure/motoraventyr/04_vindverkstaden/01_vindverkstaden.adv",
        "sdcard_bundle/otr/adventure/motoraventyr/05_hemresan/01_hemresan.adv",
        "sdcard_bundle/otr/adventure/motoraventyr/06_resurskedjan/01_lamna.adv",
        "sdcard_bundle/otr/adventure/motoraventyr/06_resurskedjan/02_fyll_pa.adv",
        "sdcard_bundle/otr/adventure/motoraventyr/06_resurskedjan/03_nollstall.adv",
    };
    for (const char *path : files) {
        std::ifstream input(path, std::ios::binary);
        CHECK(input.good());
        const std::string text((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
        accepts(text);
    }
}

std::string room(int width, int height)
{
    std::string text = "@meta\nid: size\nworld: 90\nmap_id: 90\n@map\n";
    for (int y = 0; y < height; ++y) {
        std::string row(width, '.');
        if (y == 0) row[0] = 'S';
        if (y == height - 1) row[width - 1] = 'X';
        text += row + "\n";
    }
    return text;
}

void testSizeLimits()
{
    accepts(room(128, 128));
    accepts(room(128, 4));
    accepts(room(4, 128));
    rejects(room(129, 4));
    rejects(room(4, 129));
}

}  // namespace

int main(int argc, char **argv)
{
    for (int i = 1; i < argc; ++i) {
        std::ifstream input(argv[i], std::ios::binary);
        CHECK(input.good());
        const std::string text((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
        std::fprintf(stderr, "validating %s\n", argv[i]);
        accepts(text);
    }
    if (argc > 1) {
        std::printf("Adventure text validation: %d checks passed for %d supplied files\n", checks, argc - 1);
        return 0;
    }
    testSizeLimits();
    accepts("@meta\nid: editor_test\nworld: 1\nmap_id: 1\n@map\n#####\n#S.X#\n#####\n");
    rejects("@unknown\n");
    rejects("@map\nS?X\n", 1);
    rejects("@meta\nid: bad_dest\nworld: 1\nmap_id: 1\n@map\n#####\n#S.X#\n#####\n"
            "@destinations\nbad 0,0\n", 1);
    std::string tooLarge(Adv::MAX_FILE + 1, 'x');
    rejects(tooLarge);
    testCampaign();
    std::printf("Adventure text validation: %d checks passed\n", checks);
    return 0;
}
