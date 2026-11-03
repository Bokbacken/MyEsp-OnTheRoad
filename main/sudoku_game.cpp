#include "sudoku_game.h"

#include <cstring>

#include "sudoku_puzzles.h"

namespace Sudoku {
namespace {

constexpr uint16_t kAllDigits = 0x3FE;

struct SearchFrame {
    uint8_t cell;
    uint8_t chosenDigit;
    uint16_t remaining;
};

struct Solver {
    uint8_t cells[CELL_COUNT];
    uint16_t rows[9] = {};
    uint16_t columns[9] = {};
    uint16_t boxes[9] = {};
    SearchFrame frames[CELL_COUNT];

    bool initialize(const char *puzzle)
    {
        for (unsigned cell = 0; cell < CELL_COUNT; ++cell) {
            const char ch = puzzle[cell];
            if (ch < '0' || ch > '9') return false;
            const uint8_t digit = static_cast<uint8_t>(ch - '0');
            cells[cell] = digit;
            if (!digit) continue;
            const unsigned row = cell / 9;
            const unsigned column = cell % 9;
            const unsigned box = row / 3 * 3 + column / 3;
            const uint16_t bit = static_cast<uint16_t>(1U << digit);
            if ((rows[row] | columns[column] | boxes[box]) & bit) return false;
            rows[row] |= bit;
            columns[column] |= bit;
            boxes[box] |= bit;
        }
        return true;
    }

    bool bestCell(uint8_t &cell, uint16_t &options) const
    {
        unsigned bestCount = 10;
        cell = CELL_COUNT;
        options = 0;
        for (unsigned i = 0; i < CELL_COUNT; ++i) {
            if (cells[i]) continue;
            const unsigned row = i / 9;
            const unsigned column = i % 9;
            const unsigned box = row / 3 * 3 + column / 3;
            const uint16_t candidates =
                static_cast<uint16_t>(kAllDigits & ~(rows[row] | columns[column] | boxes[box]));
            unsigned count = 0;
            for (uint16_t bits = candidates; bits; bits &= static_cast<uint16_t>(bits - 1)) ++count;
            if (!count) {
                cell = static_cast<uint8_t>(i);
                options = 0;
                return true;
            }
            if (count < bestCount) {
                bestCount = count;
                cell = static_cast<uint8_t>(i);
                options = candidates;
                if (count == 1) break;
            }
        }
        return cell != CELL_COUNT;
    }

    bool solve(const char *puzzle, uint8_t *solution)
    {
        if (!initialize(puzzle)) return false;
        uint8_t cell;
        uint16_t options;
        if (!bestCell(cell, options)) {
            std::memcpy(solution, cells, CELL_COUNT);
            return true;
        }
        if (!options) return false;
        unsigned depth = 0;
        frames[depth] = {cell, 0, options};
        for (;;) {
            SearchFrame &frame = frames[depth];
            if (!frame.remaining) {
                if (!depth) return false;
                --depth;
                SearchFrame &parent = frames[depth];
                const unsigned parentRow = parent.cell / 9;
                const unsigned parentColumn = parent.cell % 9;
                const unsigned parentBox = parentRow / 3 * 3 + parentColumn / 3;
                const uint16_t bit = static_cast<uint16_t>(1U << parent.chosenDigit);
                cells[parent.cell] = 0;
                rows[parentRow] ^= bit;
                columns[parentColumn] ^= bit;
                boxes[parentBox] ^= bit;
                parent.chosenDigit = 0;
                continue;
            }

            const uint16_t bit = static_cast<uint16_t>(frame.remaining & -frame.remaining);
            frame.remaining ^= bit;
            uint8_t digit = 1;
            while (!(bit & (1U << digit))) ++digit;
            frame.chosenDigit = digit;
            const unsigned row = frame.cell / 9;
            const unsigned column = frame.cell % 9;
            const unsigned box = row / 3 * 3 + column / 3;
            cells[frame.cell] = digit;
            rows[row] |= bit;
            columns[column] |= bit;
            boxes[box] |= bit;

            if (!bestCell(cell, options)) {
                std::memcpy(solution, cells, CELL_COUNT);
                return true;
            }
            if (!options) {
                cells[frame.cell] = 0;
                rows[row] ^= bit;
                columns[column] ^= bit;
                boxes[box] ^= bit;
                frame.chosenDigit = 0;
                continue;
            }
            ++depth;
            frames[depth] = {cell, 0, options};
        }
    }
};

bool validDifficulty(Difficulty difficulty)
{
    return static_cast<unsigned>(difficulty) < 4;
}

} // namespace

bool Game::start(Difficulty difficulty, unsigned puzzleIndex)
{
    if (!validDifficulty(difficulty) || puzzleIndex >= BLOCK_SIZE) return false;
    return startFromGivens(
        difficulty, puzzleIndex, detail::kPuzzleGivens[static_cast<unsigned>(difficulty)][puzzleIndex]);
}

bool Game::startFromGivens(Difficulty difficulty, unsigned puzzleIndex, const char *source)
{
    if (!validDifficulty(difficulty) || !source || std::strlen(source) != CELL_COUNT) return false;
    uint8_t solution[CELL_COUNT];
    Solver solver{};
    if (!solver.solve(source, solution)) return false;

    std::memcpy(givens_, source, CELL_COUNT);
    givens_[CELL_COUNT] = '\0';
    std::memcpy(solution_, solution, CELL_COUNT);
    for (unsigned cell = 0; cell < CELL_COUNT; ++cell) {
        values_[cell] = static_cast<uint8_t>(givens_[cell] - '0');
        notes_[cell] = 0;
    }
    difficulty_ = difficulty;
    puzzleIndex_ = puzzleIndex;
    started_ = true;
    historyHead_ = historySize_ = historyCursor_ = 0;
    return true;
}

bool Game::put(unsigned cell, uint8_t digit, bool notesMode)
{
    if (!started_ || cell >= CELL_COUNT || digit < 1 || digit > 9 || fixed(cell)) return false;
    uint8_t nextValue = values_[cell];
    uint16_t nextNotes = notes_[cell];
    if (notesMode) {
        if (nextValue) return false;
        nextNotes ^= static_cast<uint16_t>(1U << (digit - 1));
    } else {
        nextValue = digit;
        nextNotes = 0;
    }
    if (nextValue == values_[cell] && nextNotes == notes_[cell]) return true;
    record(cell, nextValue, nextNotes);
    return true;
}

bool Game::erase(unsigned cell)
{
    if (!started_ || cell >= CELL_COUNT || fixed(cell)) return false;
    if (!values_[cell] && !notes_[cell]) return true;
    record(cell, 0, 0);
    return true;
}

void Game::record(unsigned cell, uint8_t value, uint16_t notes)
{
    if (historyCursor_ < historySize_) historySize_ = historyCursor_;
    if (historySize_ == HISTORY_CAPACITY) {
        historyHead_ = static_cast<uint8_t>((historyHead_ + 1) % HISTORY_CAPACITY);
        --historySize_;
        --historyCursor_;
    }
    const unsigned slot = (historyHead_ + historySize_) % HISTORY_CAPACITY;
    history_[slot] = {static_cast<uint8_t>(cell), values_[cell], value, notes_[cell], notes};
    values_[cell] = value;
    notes_[cell] = notes;
    ++historySize_;
    historyCursor_ = historySize_;
}

void Game::apply(const Action &action, bool forward)
{
    values_[action.cell] = forward ? action.newValue : action.oldValue;
    notes_[action.cell] = forward ? action.newNotes : action.oldNotes;
}

bool Game::undo()
{
    if (!started_ || !historyCursor_) return false;
    const unsigned slot = (historyHead_ + historyCursor_ - 1) % HISTORY_CAPACITY;
    apply(history_[slot], false);
    --historyCursor_;
    return true;
}

bool Game::redo()
{
    if (!started_ || historyCursor_ >= historySize_) return false;
    const unsigned slot = (historyHead_ + historyCursor_) % HISTORY_CAPACITY;
    apply(history_[slot], true);
    ++historyCursor_;
    return true;
}

void Game::clearHistory()
{
    historyHead_ = historySize_ = historyCursor_ = 0;
}

bool Game::fixed(unsigned cell) const
{
    return started_ && cell < CELL_COUNT && givens_[cell] != '0';
}

uint8_t Game::value(unsigned cell) const
{
    return started_ && cell < CELL_COUNT ? values_[cell] : 0;
}

uint16_t Game::notes(unsigned cell) const
{
    return started_ && cell < CELL_COUNT ? notes_[cell] : 0;
}

bool Game::incorrect(unsigned cell) const
{
    if (!started_ || cell >= CELL_COUNT || !values_[cell]) return false;
    const unsigned row = cell / 9;
    const unsigned column = cell % 9;
    const unsigned box = row / 3 * 3 + column / 3;
    for (unsigned peer = 0; peer < CELL_COUNT; ++peer) {
        if (peer == cell || values_[peer] != values_[cell]) continue;
        const unsigned peerRow = peer / 9;
        const unsigned peerColumn = peer % 9;
        if (peerRow == row || peerColumn == column ||
            peerRow / 3 * 3 + peerColumn / 3 == box) return true;
    }
    return false;
}

bool Game::matchesSolution(unsigned cell) const
{
    return started_ && cell < CELL_COUNT && values_[cell] &&
           values_[cell] == solution_[cell];
}

bool Game::complete() const
{
    if (!started_) return false;
    for (unsigned cell = 0; cell < CELL_COUNT; ++cell)
        if (values_[cell] != solution_[cell]) return false;
    return true;
}

unsigned Game::puzzleIndex() const
{
    return puzzleIndex_;
}

Difficulty Game::difficulty() const
{
    return difficulty_;
}

const char *Game::givens() const
{
    return started_ ? givens_ : "";
}

} // namespace Sudoku
