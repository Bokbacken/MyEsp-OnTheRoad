#include "sudoku_sd.h"

#include <stdio.h>
#include <string.h>
#include <vector>

#include "board_sd.h"

#ifndef SUDOKU_SD_ROOT
#define SUDOKU_SD_ROOT "/sdcard"
#endif

namespace SudokuSd {
namespace {

constexpr const char *kFiles[] = {"easy.txt", "medium.txt", "hard.txt", "extreme.txt"};
constexpr const char *kLegacyExtremeFile = "diabolical.txt";
constexpr unsigned kLineCapacity = 128;
std::vector<uint32_t> s_blockOffsets[4];
char s_paths[4][96]{};
uint32_t s_indexGeneration = UINT32_MAX;
bool s_indexReady = false;

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
        long offset;
        while ((offset = ftell(file)) >= 0 && nextLine(file, line)) {
            if (parseRecord(line, puzzle) && counts[difficulty] != UINT32_MAX) {
                if (counts[difficulty] % Sudoku::BLOCK_SIZE == 0 &&
                    static_cast<unsigned long>(offset) <= UINT32_MAX)
                    s_blockOffsets[difficulty].push_back(static_cast<uint32_t>(offset));
                ++counts[difficulty];
            }
        }
        fclose(file);
    }
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
