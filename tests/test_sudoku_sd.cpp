#include <cstdio>
#include <cstring>
#include <sys/stat.h>
#include <utime.h>

#include "board_sd.h"
#include "sudoku_game.h"
#include "sudoku_puzzles.h"
#include "sudoku_sd.h"

static bool cardMounted = true;

namespace BoardSd {
bool beginTransfer() { return cardMounted; }
bool mounted() { return cardMounted; }
void endTransfer() {}
Info info() { Info current{}; current.generation = 1; return current; }
}

#define CHECK(expression)                                                                    \
    do {                                                                                     \
        if (!(expression)) {                                                                 \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expression);      \
            return 1;                                                                        \
        }                                                                                    \
    } while (0)

int main()
{
    uint32_t counts[4] = {};
    CHECK(SudokuSd::hasSources());
    CHECK(SudokuSd::needsScanNotice());
    CHECK(SudokuSd::scan(counts));
    CHECK(counts[0] == 102);
    CHECK(counts[1] == 1);
    CHECK(counts[2] == 0);
    CHECK(counts[3] == 1);
    char cachePath[256];
    std::snprintf(cachePath, sizeof(cachePath), "%s/otr/sudoku/easy.txt.sidx", SUDOKU_SD_ROOT);
    FILE *cache = std::fopen(cachePath, "rb");
    CHECK(cache);
    std::fclose(cache);
    CHECK(SudokuSd::scan(counts));
    CHECK(counts[0] == 102 && counts[1] == 1 && counts[3] == 1);
    CHECK(!SudokuSd::needsScanNotice());
    CHECK(!SudokuSd::changed());

    char puzzles[Sudoku::BLOCK_SIZE][Sudoku::CELL_COUNT + 1]{};
    uint32_t loaded = 0;
    CHECK(SudokuSd::loadBlock(Sudoku::Difficulty::EASY, 0, puzzles, loaded));
    CHECK(loaded == Sudoku::BLOCK_SIZE);
    CHECK(std::strcmp(puzzles[0], Sudoku::detail::kPuzzleGivens[0][0]) == 0);
    CHECK(std::strcmp(puzzles[99], Sudoku::detail::kPuzzleGivens[0][1]) == 0);
    CHECK(SudokuSd::loadBlock(Sudoku::Difficulty::EASY, 1, puzzles, loaded));
    CHECK(loaded == 2);
    CHECK(std::strcmp(puzzles[0], Sudoku::detail::kPuzzleGivens[0][1]) == 0);
    CHECK(std::strcmp(puzzles[1], Sudoku::detail::kPuzzleGivens[0][0]) == 0);
    char easyPath[256];
    std::snprintf(easyPath, sizeof(easyPath), "%s/otr/sudoku/easy.txt", SUDOKU_SD_ROOT);
    FILE *easy = std::fopen(easyPath, "ab");
    CHECK(easy);
    std::fprintf(easy, "fedcba654321 %s 4.0\n", Sudoku::detail::kPuzzleGivens[0][2]);
    std::fclose(easy);
    CHECK(SudokuSd::changed());
    CHECK(SudokuSd::needsScanNotice());
    CHECK(SudokuSd::scan(counts));
    CHECK(counts[0] == 103);
    CHECK(!SudokuSd::needsScanNotice());
    CHECK(SudokuSd::loadBlock(Sudoku::Difficulty::EASY, 1, puzzles, loaded));
    CHECK(loaded == 3);
    CHECK(std::strcmp(puzzles[2], Sudoku::detail::kPuzzleGivens[0][2]) == 0);
    CHECK(SudokuSd::loadBlock(Sudoku::Difficulty::EXTREME, 0, puzzles, loaded));
    CHECK(loaded == 1);
    CHECK(std::strcmp(puzzles[0], Sudoku::detail::kPuzzleGivens[3][0]) == 0);
    CHECK(!SudokuSd::loadBlock(Sudoku::Difficulty::HARD, 0, puzzles, loaded));
    CHECK(!SudokuSd::loadBlock(Sudoku::Difficulty::EASY, 2, puzzles, loaded));

    char path[256];
    std::snprintf(path, sizeof(path), "%s/otr/sudoku/medium.txt", SUDOKU_SD_ROOT);
    FILE *medium = std::fopen(path, "wb");
    CHECK(medium);
    std::fprintf(medium, "012345abcdef %s 1.0\n", Sudoku::detail::kPuzzleGivens[1][1]);
    std::fclose(medium);
    struct stat mediumInfo{};
    CHECK(stat(path, &mediumInfo) == 0);
    struct utimbuf mediumTime{mediumInfo.st_atime, mediumInfo.st_mtime + 10};
    CHECK(utime(path, &mediumTime) == 0);
    CHECK(SudokuSd::changed());
    CHECK(SudokuSd::scan(counts));
    CHECK(!SudokuSd::changed());
    CHECK(SudokuSd::loadBlock(Sudoku::Difficulty::MEDIUM, 0, puzzles, loaded));
    CHECK(loaded == 1);
    CHECK(std::strcmp(puzzles[0], Sudoku::detail::kPuzzleGivens[1][1]) == 0);

    std::snprintf(path, sizeof(path), "%s/otr/sudoku/extreme.txt", SUDOKU_SD_ROOT);
    FILE *extreme = std::fopen(path, "wb");
    CHECK(extreme);
    std::fprintf(extreme, "fedcba654321 %s 2.0\n", Sudoku::detail::kPuzzleGivens[3][0]);
    std::fprintf(extreme, "abcdef123456 %s 3.0\n", Sudoku::detail::kPuzzleGivens[3][1]);
    std::fclose(extreme);
    CHECK(SudokuSd::changed());
    CHECK(SudokuSd::scan(counts));
    CHECK(!SudokuSd::changed());
    CHECK(counts[3] == 2);
    CHECK(SudokuSd::loadBlock(Sudoku::Difficulty::EXTREME, 0, puzzles, loaded));
    CHECK(loaded == 2);
    CHECK(std::strcmp(puzzles[1], Sudoku::detail::kPuzzleGivens[3][1]) == 0);

    cardMounted = false;
    CHECK(!SudokuSd::changed());
    CHECK(!SudokuSd::scan(counts));
    CHECK(!SudokuSd::loadBlock(Sudoku::Difficulty::EASY, 0, puzzles, loaded));
    cardMounted = true;
    const char *files[] = {"easy.txt", "medium.txt", "hard.txt", "extreme.txt", "diabolical.txt"};
    for (const char *name : files) {
        std::snprintf(path, sizeof(path), "%s/otr/sudoku/%s", SUDOKU_SD_ROOT, name);
        std::remove(path);
        std::snprintf(path, sizeof(path), "%s/otr/sudoku/%s.sidx", SUDOKU_SD_ROOT, name);
        std::remove(path);
    }
    CHECK(!SudokuSd::hasSources());
    CHECK(!SudokuSd::needsScanNotice());
    return 0;
}
