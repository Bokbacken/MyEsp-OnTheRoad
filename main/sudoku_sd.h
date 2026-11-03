#pragma once

#include <stdint.h>

#include "sudoku_game.h"

namespace SudokuSd {

bool scan(uint32_t counts[4]);
bool changed();
bool hasSources();
bool needsScanNotice();
bool loadBlock(Sudoku::Difficulty difficulty, uint32_t block,
               char puzzles[Sudoku::BLOCK_SIZE][Sudoku::CELL_COUNT + 1], uint32_t &count);

} // namespace SudokuSd
