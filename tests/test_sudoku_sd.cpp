#include <cstdio>
#include <cstring>

#include "board_sd.h"
#include "sudoku_game.h"
#include "sudoku_puzzles.h"
#include "sudoku_sd.h"

static bool cardMounted = true;

namespace BoardSd {
bool beginTransfer() { return cardMounted; }
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
    CHECK(SudokuSd::scan(counts));
    CHECK(counts[0] == 102);
    CHECK(counts[1] == 1);
    CHECK(counts[2] == 0);
    CHECK(counts[3] == 1);

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
    CHECK(SudokuSd::loadBlock(Sudoku::Difficulty::EXTREME, 0, puzzles, loaded));
    CHECK(loaded == 1);
    CHECK(std::strcmp(puzzles[0], Sudoku::detail::kPuzzleGivens[3][0]) == 0);
    CHECK(!SudokuSd::loadBlock(Sudoku::Difficulty::HARD, 0, puzzles, loaded));
    CHECK(!SudokuSd::loadBlock(Sudoku::Difficulty::EASY, 2, puzzles, loaded));

    char path[256];
    std::snprintf(path, sizeof(path), "%s/otr/sudoku/extreme.txt", SUDOKU_SD_ROOT);
    FILE *extreme = std::fopen(path, "wb");
    CHECK(extreme);
    std::fprintf(extreme, "fedcba654321 %s 2.0\n", Sudoku::detail::kPuzzleGivens[3][0]);
    std::fprintf(extreme, "abcdef123456 %s 3.0\n", Sudoku::detail::kPuzzleGivens[3][1]);
    std::fclose(extreme);
    CHECK(SudokuSd::scan(counts));
    CHECK(counts[3] == 2);
    CHECK(SudokuSd::loadBlock(Sudoku::Difficulty::EXTREME, 0, puzzles, loaded));
    CHECK(loaded == 2);
    CHECK(std::strcmp(puzzles[1], Sudoku::detail::kPuzzleGivens[3][1]) == 0);

    cardMounted = false;
    CHECK(!SudokuSd::scan(counts));
    CHECK(!SudokuSd::loadBlock(Sudoku::Difficulty::EASY, 0, puzzles, loaded));
    return 0;
}
