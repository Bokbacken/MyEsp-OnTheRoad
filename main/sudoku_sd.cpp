#include "sudoku_sd.h"

#include <stdio.h>
#include <string.h>
#include <vector>
#include <sys/stat.h>

#include "board_sd.h"

#ifndef SUDOKU_SD_ROOT
#define SUDOKU_SD_ROOT "/sdcard"
#endif

namespace SudokuSd {
namespace {

constexpr const char *kFiles[] = {"easy.txt", "medium.txt", "hard.txt", "extreme.txt"};
constexpr const char *kLegacyExtremeFile = "diabolical.txt";
constexpr unsigned kLineCapacity = 128;
constexpr uint32_t kIndexMagic = 0x53445549;
constexpr uint32_t kIndexVersion = 1;
std::vector<uint32_t> s_blockOffsets[4];
char s_paths[4][96]{};
uint32_t s_indexGeneration = UINT32_MAX;
bool s_indexReady = false;
struct FileSignature {
    char path[96]{};
    uint64_t size = 0;
    int64_t modified = 0;
    bool present = false;
};
FileSignature s_signatures[4]{};
bool s_signaturesReady = false;

bool hexDigit(char c)
{
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

bool parseRecord(char *line, char puzzle[Sudoku::CELL_COUNT + 1])
{
    size_t length = strlen(line);
    while (length && (line[length - 1] == '\n' || line[length - 1] == '\r')) line[--length] = '\0';
    if (length < 12 + 1 + Sudoku::CELL_COUNT + 1) return false;
    for (unsigned i = 0; i < 12; ++i)
        if (!hexDigit(line[i])) return false;
    if (line[12] != ' ') return false;
    for (unsigned i = 0; i < Sudoku::CELL_COUNT; ++i)
        if (line[13 + i] < '0' || line[13 + i] > '9') return false;
    size_t pos = 13 + Sudoku::CELL_COUNT;
    if (line[pos++] != ' ') return false;
    while (line[pos] == ' ') ++pos;
    const size_t ratingStart = pos;
    while (line[pos] >= '0' && line[pos] <= '9') ++pos;
    if (pos == ratingStart) return false;
    if (line[pos] == '.') {
        ++pos;
        if (line[pos] < '0' || line[pos] > '9') return false;
        ++pos;
    }
    if (line[pos] != '\0') return false;
    memcpy(puzzle, line + 13, Sudoku::CELL_COUNT);
    puzzle[Sudoku::CELL_COUNT] = '\0';
    return true;
}

void makePath(char path[96], unsigned difficulty, bool legacyExtreme = false)
{
    const char *filename = legacyExtreme ? kLegacyExtremeFile : kFiles[difficulty];
    snprintf(path, 96, "%s/otr/sudoku/%s", SUDOKU_SD_ROOT, filename);
}

bool currentSource(unsigned difficulty, char path[96], struct stat &info)
{
    makePath(path, difficulty);
    if (stat(path, &info) == 0) return true;
    if (difficulty == static_cast<unsigned>(Sudoku::Difficulty::EXTREME)) {
        makePath(path, difficulty, true);
        if (stat(path, &info) == 0) return true;
    }
    return false;
}

FILE *openDifficulty(unsigned difficulty, char path[96])
{
    makePath(path, difficulty);
    FILE *file = fopen(path, "rb");
    if (!file && difficulty == static_cast<unsigned>(Sudoku::Difficulty::EXTREME)) {
        makePath(path, difficulty, true);
        file = fopen(path, "rb");
    }
    return file;
}

void makeIndexPath(char path[400], const char *sourcePath)
{
    snprintf(path, 400, "%s.sidx", sourcePath);
}

bool loadIndex(const char *sourcePath, uint64_t fileSize, int64_t modified,
               uint32_t &puzzleCount, std::vector<uint32_t> &offsets)
{
    char path[400];
    makeIndexPath(path, sourcePath);
    FILE *file = fopen(path, "rb");
    if (!file) return false;

    uint32_t magic = 0, version = 0, cachedCount = 0, offsetCount = 0;
    uint64_t cachedSize = 0;
    int64_t cachedModified = 0;
    bool valid = fread(&magic, sizeof(magic), 1, file) == 1 &&
                 fread(&version, sizeof(version), 1, file) == 1 &&
                 fread(&cachedSize, sizeof(cachedSize), 1, file) == 1 &&
                 fread(&cachedModified, sizeof(cachedModified), 1, file) == 1 &&
                 fread(&cachedCount, sizeof(cachedCount), 1, file) == 1 &&
                 fread(&offsetCount, sizeof(offsetCount), 1, file) == 1;
    const uint64_t maxRecords = fileSize / (12 + 1 + Sudoku::CELL_COUNT + 1 + 1);
    const uint64_t expectedOffsets =
        (static_cast<uint64_t>(cachedCount) + Sudoku::BLOCK_SIZE - 1) / Sudoku::BLOCK_SIZE;
    if (!valid || magic != kIndexMagic || version != kIndexVersion ||
        cachedSize != fileSize || cachedModified != modified ||
        cachedCount > maxRecords || offsetCount > expectedOffsets ||
        (fileSize <= UINT32_MAX && offsetCount != expectedOffsets)) {
        fclose(file);
        return false;
    }

    std::vector<uint32_t> cachedOffsets(offsetCount);
    if (offsetCount && fread(cachedOffsets.data(), sizeof(uint32_t), offsetCount, file) != offsetCount)
        valid = false;
    if (valid && fgetc(file) != EOF) valid = false;
    if (ferror(file)) valid = false;
    fclose(file);
    if (!valid) return false;

    uint32_t previous = 0;
    for (uint32_t i = 0; i < offsetCount; ++i) {
        const uint32_t offset = cachedOffsets[i];
        if (offset >= fileSize || (i && offset <= previous)) return false;
        previous = offset;
    }
    puzzleCount = cachedCount;
    offsets.swap(cachedOffsets);
    return true;
}

bool filesChanged()
{
    if (!s_signaturesReady) return true;
    for (unsigned difficulty = 0; difficulty < 4; ++difficulty) {
        char path[96];
        struct stat info{};
        const bool present = currentSource(difficulty, path, info);
        const FileSignature &signature = s_signatures[difficulty];
        if (present != signature.present || strcmp(path, signature.path) != 0) return true;
        if (present && (static_cast<uint64_t>(info.st_size) != signature.size ||
                        static_cast<int64_t>(info.st_mtime) != signature.modified))
            return true;
    }
    return false;
}

void saveIndex(const char *sourcePath, uint64_t fileSize, int64_t modified,
               uint32_t puzzleCount, const std::vector<uint32_t> &offsets)
{
    char path[400], temporary[404];
    makeIndexPath(path, sourcePath);
    snprintf(temporary, sizeof(temporary), "%s.tmp", path);
    remove(temporary);
    FILE *file = fopen(temporary, "wb");
    if (!file) return;

    const uint32_t magic = kIndexMagic, version = kIndexVersion;
    const uint32_t offsetCount = static_cast<uint32_t>(offsets.size());
    bool ok = fwrite(&magic, sizeof(magic), 1, file) == 1 &&
              fwrite(&version, sizeof(version), 1, file) == 1 &&
              fwrite(&fileSize, sizeof(fileSize), 1, file) == 1 &&
              fwrite(&modified, sizeof(modified), 1, file) == 1 &&
              fwrite(&puzzleCount, sizeof(puzzleCount), 1, file) == 1 &&
              fwrite(&offsetCount, sizeof(offsetCount), 1, file) == 1;
    if (ok && offsetCount)
        ok = fwrite(offsets.data(), sizeof(uint32_t), offsetCount, file) == offsetCount;
    if (fflush(file) != 0) ok = false;
    if (fclose(file) != 0) ok = false;
    if (ok) {
        remove(path);
        if (rename(temporary, path) == 0) return;
    }
    remove(temporary);
}

bool nextLine(FILE *file, char line[kLineCapacity])
{
    if (!fgets(line, kLineCapacity, file)) return false;
    const size_t length = strlen(line);
    if (length == kLineCapacity - 1 && line[length - 1] != '\n' && !feof(file)) {
        int c;
        do {
            c = fgetc(file);
        } while (c != '\n' && c != EOF);
        line[0] = '\0';
    }
    return true;
}

} // namespace

bool changed()
{
    return BoardSd::mounted() && filesChanged();
}

bool scan(uint32_t counts[4])
{
    if (!counts) return false;
    memset(counts, 0, sizeof(uint32_t) * 4);
    BoardSd::Transfer transfer;
    if (!transfer.active) return false;
    s_indexReady = false;
    for (auto &offsets : s_blockOffsets) offsets.clear();

    char line[kLineCapacity];
    char puzzle[Sudoku::CELL_COUNT + 1];
    for (unsigned difficulty = 0; difficulty < 4; ++difficulty) {
        FILE *file = openDifficulty(difficulty, s_paths[difficulty]);
        if (!file) continue;
        struct stat sourceInfo{};
        const bool cacheable = stat(s_paths[difficulty], &sourceInfo) == 0 && sourceInfo.st_size >= 0;
        const uint64_t fileSize = cacheable ? static_cast<uint64_t>(sourceInfo.st_size) : 0;
        const int64_t modified = cacheable ? static_cast<int64_t>(sourceInfo.st_mtime) : 0;
        if (cacheable &&
            loadIndex(s_paths[difficulty], fileSize, modified, counts[difficulty],
                      s_blockOffsets[difficulty])) {
            fclose(file);
            continue;
        }

        long offset;
        while ((offset = ftell(file)) >= 0 && nextLine(file, line)) {
            if (parseRecord(line, puzzle) && counts[difficulty] != UINT32_MAX) {
                if (counts[difficulty] % Sudoku::BLOCK_SIZE == 0 &&
                    static_cast<unsigned long>(offset) <= UINT32_MAX)
                    s_blockOffsets[difficulty].push_back(static_cast<uint32_t>(offset));
                ++counts[difficulty];
            }
        }
        const bool readOk = !ferror(file);
        fclose(file);
        struct stat afterInfo{};
        if (cacheable && readOk && stat(s_paths[difficulty], &afterInfo) == 0 &&
            static_cast<uint64_t>(afterInfo.st_size) == fileSize &&
            static_cast<int64_t>(afterInfo.st_mtime) == modified)
            saveIndex(s_paths[difficulty], fileSize, modified, counts[difficulty],
                      s_blockOffsets[difficulty]);
    }
    for (unsigned difficulty = 0; difficulty < 4; ++difficulty) {
        FileSignature &signature = s_signatures[difficulty];
        struct stat info{};
        signature.present = currentSource(difficulty, signature.path, info);
        signature.size = signature.present ? static_cast<uint64_t>(info.st_size) : 0;
        signature.modified = signature.present ? static_cast<int64_t>(info.st_mtime) : 0;
    }
    s_signaturesReady = true;
    s_indexGeneration = BoardSd::info().generation;
    s_indexReady = true;
    return true;
}

bool loadBlock(Sudoku::Difficulty difficulty, uint32_t block,
               char puzzles[Sudoku::BLOCK_SIZE][Sudoku::CELL_COUNT + 1], uint32_t &count)
{
    const unsigned d = static_cast<unsigned>(difficulty);
    count = 0;
    if (d >= 4 || !puzzles || !s_indexReady ||
        s_indexGeneration != BoardSd::info().generation ||
        block >= s_blockOffsets[d].size()) return false;
    BoardSd::Transfer transfer;
    if (!transfer.active) return false;

    FILE *file = fopen(s_paths[d], "rb");
    if (!file || fseek(file, static_cast<long>(s_blockOffsets[d][block]), SEEK_SET) != 0) {
        if (file) fclose(file);
        return false;
    }
    char line[kLineCapacity];
    char puzzle[Sudoku::CELL_COUNT + 1];
    while (count < Sudoku::BLOCK_SIZE && nextLine(file, line)) {
        if (!parseRecord(line, puzzle)) continue;
        memcpy(puzzles[count++], puzzle, sizeof(puzzle));
    }
    fclose(file);
    return count > 0;
}

} // namespace SudokuSd
