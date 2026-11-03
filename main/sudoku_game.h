#pragma once

#include <cstdint>

namespace Sudoku {

enum class Difficulty : uint8_t { EASY, MEDIUM, HARD, EXTREME };
constexpr unsigned BLOCK_SIZE = 100;
constexpr unsigned CELL_COUNT = 81;
constexpr unsigned HISTORY_CAPACITY = 128;

class Game {
public:
    bool start(Difficulty difficulty, unsigned puzzleIndex);
    bool startFromGivens(Difficulty difficulty, unsigned puzzleIndex, const char *givens);
    bool put(unsigned cell, uint8_t digit, bool notesMode);
    bool erase(unsigned cell);
    bool undo();
    bool redo();
    void clearHistory();
    bool fixed(unsigned cell) const;
    uint8_t value(unsigned cell) const;
    uint16_t notes(unsigned cell) const;
    bool incorrect(unsigned cell) const;
    bool matchesSolution(unsigned cell) const;
    bool complete() const;
    unsigned puzzleIndex() const;
    Difficulty difficulty() const;
    const char *givens() const;

private:
    struct Action {
        uint8_t cell;
        uint8_t oldValue;
        uint8_t newValue;
        uint16_t oldNotes;
        uint16_t newNotes;
    };

    void record(unsigned cell, uint8_t value, uint16_t notes);
    void apply(const Action &action, bool forward);
    bool started_ = false;
    Difficulty difficulty_ = Difficulty::EASY;
    unsigned puzzleIndex_ = 0;
    char givens_[CELL_COUNT + 1] = {};
    uint8_t solution_[CELL_COUNT] = {};
    uint8_t values_[CELL_COUNT] = {};
    uint16_t notes_[CELL_COUNT] = {};
    Action history_[HISTORY_CAPACITY] = {};
    uint8_t historyHead_ = 0;
    uint8_t historySize_ = 0;
    uint8_t historyCursor_ = 0;
};

} // namespace Sudoku
