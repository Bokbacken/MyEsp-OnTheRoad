#include <cstdio>
#include <cstring>

#include "sudoku_game.h"
#include "sudoku_puzzles.h"

static int failures = 0;
static int checks = 0;

#define CHECK(expression)                                                                    \
    do {                                                                                     \
        ++checks;                                                                            \
        if (!(expression)) {                                                                 \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #expression);                \
            ++failures;                                                                      \
        }                                                                                    \
    } while (0)

static unsigned solutionCount(char board[81], unsigned rows[9], unsigned columns[9],
                              unsigned boxes[9], char solved[81] = nullptr)
{
    unsigned bestCell = 81;
    unsigned bestOptions = 0;
    unsigned bestCount = 10;
    for (unsigned cell = 0; cell < 81; ++cell) {
        if (board[cell] != '0') continue;
        const unsigned row = cell / 9;
        const unsigned column = cell % 9;
        const unsigned box = row / 3 * 3 + column / 3;
        const unsigned options = 0x3FE & ~(rows[row] | columns[column] | boxes[box]);
        unsigned count = 0;
        for (unsigned bits = options; bits; bits &= bits - 1) ++count;
        if (!count) return 0;
        if (count < bestCount) {
            bestCell = cell;
            bestOptions = options;
            bestCount = count;
            if (count == 1) break;
        }
    }
    if (bestCell == 81) {
        if (solved) std::memcpy(solved, board, 81);
        return 1;
    }

    const unsigned row = bestCell / 9;
    const unsigned column = bestCell % 9;
    const unsigned box = row / 3 * 3 + column / 3;
    unsigned count = 0;
    for (unsigned options = bestOptions; options && count < 2; options &= options - 1) {
        const unsigned bit = options & (0U - options);
        unsigned digit = 1;
        while (!(bit & (1U << digit))) ++digit;
        board[bestCell] = static_cast<char>('0' + digit);
        rows[row] |= bit;
        columns[column] |= bit;
        boxes[box] |= bit;
        count += solutionCount(board, rows, columns, boxes, solved);
        rows[row] ^= bit;
        columns[column] ^= bit;
        boxes[box] ^= bit;
        board[bestCell] = '0';
    }
    return count;
}

static bool hasUniqueSolution(const char *puzzle)
{
    char board[81];
    std::memcpy(board, puzzle, sizeof(board));
    unsigned rows[9] = {};
    unsigned columns[9] = {};
    unsigned boxes[9] = {};
    for (unsigned cell = 0; cell < 81; ++cell) {
        if (board[cell] == '0') continue;
        const unsigned digit = static_cast<unsigned>(board[cell] - '0');
        const unsigned bit = 1U << digit;
        const unsigned row = cell / 9;
        const unsigned column = cell % 9;
        const unsigned box = row / 3 * 3 + column / 3;
        if ((rows[row] | columns[column] | boxes[box]) & bit) return false;
        rows[row] |= bit;
        columns[column] |= bit;
        boxes[box] |= bit;
    }
    return solutionCount(board, rows, columns, boxes) == 1;
}

static bool solvePuzzle(const char *puzzle, char solved[81])
{
    char board[81];
    std::memcpy(board, puzzle, sizeof(board));
    unsigned rows[9] = {};
    unsigned columns[9] = {};
    unsigned boxes[9] = {};
    for (unsigned cell = 0; cell < 81; ++cell) {
        if (board[cell] == '0') continue;
        const unsigned digit = static_cast<unsigned>(board[cell] - '0');
        const unsigned bit = 1U << digit;
        const unsigned row = cell / 9;
        const unsigned column = cell % 9;
        const unsigned box = row / 3 * 3 + column / 3;
        if ((rows[row] | columns[column] | boxes[box]) & bit) return false;
        rows[row] |= bit;
        columns[column] |= bit;
        boxes[box] |= bit;
    }
    return solutionCount(board, rows, columns, boxes, solved) == 1;
}

static bool canPlaceWithoutConflict(const Sudoku::Game &game, unsigned cell, uint8_t digit);

static void testBundledPuzzleUniquenessAndLoading()
{
    Sudoku::Game game;
    for (unsigned difficulty = 0; difficulty < 4; ++difficulty) {
        for (unsigned index = 0; index < Sudoku::BLOCK_SIZE; ++index) {
            const char *puzzle = Sudoku::detail::kPuzzleGivens[difficulty][index];
            CHECK(std::strlen(puzzle) == 81);
            CHECK(hasUniqueSolution(puzzle));
            CHECK(game.start(static_cast<Sudoku::Difficulty>(difficulty), index));
            CHECK(std::strcmp(game.givens(), puzzle) == 0);
            CHECK(game.puzzleIndex() == index);
            CHECK(static_cast<unsigned>(game.difficulty()) == difficulty);
            CHECK(!game.complete());
        }
    }
}

static void testInputAndFixedCells()
{
    Sudoku::Game game;
    CHECK(!game.undo());
    CHECK(!game.put(0, 1, false));
    CHECK(!game.erase(0));
    CHECK(!game.start(static_cast<Sudoku::Difficulty>(4), 0));
    CHECK(!game.start(Sudoku::Difficulty::EASY, Sudoku::BLOCK_SIZE));
    CHECK(game.start(Sudoku::Difficulty::EASY, 0));
    const char *givens = game.givens();
    char savedGivens[82];
    std::memcpy(savedGivens, givens, sizeof(savedGivens));
    CHECK(!game.start(static_cast<Sudoku::Difficulty>(255), 3));
    CHECK(std::strcmp(savedGivens, game.givens()) == 0);
    CHECK(game.difficulty() == Sudoku::Difficulty::EASY && game.puzzleIndex() == 0);
    unsigned fixed = 0;
    while (fixed < 81 && givens[fixed] == '0') ++fixed;
    unsigned editable = 0;
    while (editable < 81 && givens[editable] != '0') ++editable;
    CHECK(fixed < 81 && editable < 81);
    const uint8_t original = game.value(fixed);
    CHECK(game.fixed(fixed));
    CHECK(!game.put(fixed, 1, false));
    CHECK(!game.put(fixed, 1, true));
    CHECK(!game.erase(fixed));
    CHECK(game.value(fixed) == original);
    CHECK(!game.fixed(editable));
    CHECK(!game.put(81, 1, false));
    CHECK(!game.erase(81));
    CHECK(!game.put(editable, 0, false));
    CHECK(!game.put(editable, 10, true));
    CHECK(game.value(editable) == 0 && game.notes(editable) == 0);
}

static void testNotesEntryAndErase()
{
    Sudoku::Game game;
    CHECK(game.start(Sudoku::Difficulty::EASY, 0));
    const char *givens = game.givens();
    unsigned empty = 0;
    while (givens[empty] != '0') ++empty;
    CHECK(game.put(empty, 3, true));
    CHECK(game.notes(empty) == (1U << 2));
    CHECK(game.value(empty) == 0);
    CHECK(!game.incorrect(empty));
    CHECK(game.put(empty, 3, true));
    CHECK(game.notes(empty) == 0);
    CHECK(game.put(empty, 4, true));
    CHECK(game.put(empty, 7, true));
    CHECK(game.notes(empty) == ((1U << 3) | (1U << 6)));
    CHECK(game.put(empty, 1, true));
    CHECK(game.put(empty, 9, true));
    CHECK(game.notes(empty) == ((1U << 0) | (1U << 3) | (1U << 6) | (1U << 8)));
    CHECK(game.put(empty, 5, false));
    CHECK(game.value(empty) == 5);
    CHECK(game.notes(empty) == 0);
    CHECK(game.erase(empty));
    CHECK(game.value(empty) == 0);
    CHECK(!game.incorrect(empty));
    CHECK(game.put(empty, 4, true));
    CHECK(game.put(empty, 4, true));
    CHECK(game.notes(empty) == 0);
    CHECK(game.erase(empty));
    CHECK(game.notes(empty) == 0);
}

static void testCorrectnessAndCompletion()
{
    Sudoku::Game game;
    CHECK(game.start(Sudoku::Difficulty::MEDIUM, 0));
    char solution[81];
    CHECK(solvePuzzle(game.givens(), solution));
    unsigned empty = 0;
    while (game.givens()[empty] != '0') ++empty;
    bool sawLegalWrong = false;
    for (uint8_t digit = 1; digit <= 9; ++digit) {
        if (digit == static_cast<uint8_t>(solution[empty] - '0') ||
            !canPlaceWithoutConflict(game, empty, digit)) continue;
        CHECK(game.put(empty, digit, false));
        CHECK(!game.incorrect(empty));
        CHECK(game.erase(empty));
        sawLegalWrong = true;
        break;
    }
    CHECK(sawLegalWrong);
    CHECK(!game.complete());
    for (unsigned cell = 0; cell < 81; ++cell) {
        if (game.fixed(cell)) continue;
        CHECK(game.put(cell, static_cast<uint8_t>(solution[cell] - '0'), false));
        CHECK(!game.incorrect(cell));
    }
    CHECK(game.complete());
    CHECK(!game.incorrect(empty));
}

static bool canPlaceWithoutConflict(const Sudoku::Game &game, unsigned cell, uint8_t digit)
{
    const unsigned row = cell / 9;
    const unsigned column = cell % 9;
    const unsigned box = row / 3 * 3 + column / 3;
    for (unsigned peer = 0; peer < Sudoku::CELL_COUNT; ++peer) {
        if (peer == cell || game.value(peer) != digit) continue;
        const unsigned peerRow = peer / 9;
        const unsigned peerColumn = peer % 9;
        if (peerRow == row || peerColumn == column ||
            peerRow / 3 * 3 + peerColumn / 3 == box) return false;
    }
    return true;
}

static bool findConflictPair(const Sudoku::Game &game, unsigned unit, unsigned &first,
                             unsigned &second, uint8_t &digit)
{
    for (unsigned a = 0; a < Sudoku::CELL_COUNT; ++a) {
        if (game.fixed(a)) continue;
        for (unsigned b = a + 1; b < Sudoku::CELL_COUNT; ++b) {
            if (game.fixed(b)) continue;
            const unsigned rowA = a / 9, columnA = a % 9;
            const unsigned rowB = b / 9, columnB = b % 9;
            const bool sharesUnit =
                unit == 0 ? rowA == rowB && columnA / 3 != columnB / 3 :
                unit == 1 ? columnA == columnB && rowA / 3 != rowB / 3 :
                            rowA / 3 == rowB / 3 && columnA / 3 == columnB / 3 &&
                                rowA != rowB && columnA != columnB;
            if (!sharesUnit) continue;
            for (uint8_t candidate = 1; candidate <= 9; ++candidate) {
                if (!canPlaceWithoutConflict(game, a, candidate) ||
                    !canPlaceWithoutConflict(game, b, candidate)) continue;
                first = a;
                second = b;
                digit = candidate;
                return true;
            }
        }
    }
    return false;
}

static void testRuleBasedIncorrect()
{
    Sudoku::Game game;
    CHECK(game.start(Sudoku::Difficulty::EASY, 0));
    CHECK(game.put(0, 2, false));
    CHECK(!game.incorrect(0));
    CHECK(game.erase(0));

    for (unsigned unit = 0; unit < 3; ++unit) {
        CHECK(game.start(Sudoku::Difficulty::EASY, 0));
        unsigned first = 0, second = 0;
        uint8_t digit = 0;
        CHECK(findConflictPair(game, unit, first, second, digit));
        if (!digit) continue;
        CHECK(game.put(first, digit, false));
        CHECK(!game.incorrect(first));
        CHECK(game.put(second, digit, false));
        CHECK(game.incorrect(first) && game.incorrect(second));
        CHECK(game.undo());
        CHECK(!game.incorrect(first) && !game.incorrect(second));
    }
}

static void testUndoRedoAndBranching()
{
    Sudoku::Game game;
    CHECK(game.start(Sudoku::Difficulty::EASY, 0));
    unsigned cell = 0;
    while (game.givens()[cell] != '0') ++cell;
    CHECK(!game.undo());
    CHECK(game.put(cell, 2, true));
    CHECK(game.put(cell, 3, false));
    CHECK(game.value(cell) == 3 && game.notes(cell) == 0);
    CHECK(game.undo());
    CHECK(game.value(cell) == 0 && game.notes(cell) == (1U << 1));
    CHECK(game.undo());
    CHECK(game.value(cell) == 0 && game.notes(cell) == 0);
    CHECK(game.redo());
    CHECK(game.notes(cell) == (1U << 1));
    CHECK(game.redo());
    CHECK(game.value(cell) == 3 && game.notes(cell) == 0);
    CHECK(!game.redo());
    CHECK(game.undo());
    CHECK(game.put(cell, 8, true));
    CHECK(!game.redo());
    CHECK(game.notes(cell) == ((1U << 1) | (1U << 7)));
    CHECK(game.start(Sudoku::Difficulty::HARD, 4));
    CHECK(!game.undo());
    unsigned restoredCell = 0;
    while (game.givens()[restoredCell] != '0') ++restoredCell;
    CHECK(game.put(restoredCell, 6, false));
    unsigned restoredNotesCell = restoredCell + 1;
    while (restoredNotesCell < 81 && game.fixed(restoredNotesCell)) ++restoredNotesCell;
    if (restoredNotesCell < 81) CHECK(game.put(restoredNotesCell, 2, true));
    game.clearHistory();
    CHECK(!game.undo());
    CHECK(game.value(restoredCell) == 6);
    if (restoredNotesCell < 81) CHECK(game.notes(restoredNotesCell) == (1U << 1));
}

static void testHistoryCapacityAndImmutability()
{
    Sudoku::Game game;
    CHECK(game.start(Sudoku::Difficulty::EASY, 0));
    unsigned emptyCells[81];
    unsigned count = 0;
    for (unsigned cell = 0; cell < 81; ++cell)
        if (!game.fixed(cell)) emptyCells[count++] = cell;
    for (unsigned action = 0; action < Sudoku::HISTORY_CAPACITY + 20; ++action) {
        const unsigned cell = emptyCells[action % count];
        CHECK(game.put(cell, static_cast<uint8_t>(action % 9 + 1), false));
    }
    for (unsigned action = 0; action < Sudoku::HISTORY_CAPACITY; ++action) CHECK(game.undo());
    CHECK(!game.undo());
    for (unsigned action = 0; action < Sudoku::HISTORY_CAPACITY; ++action) CHECK(game.redo());
    CHECK(!game.redo());
    const char *givens = game.givens();
    for (unsigned cell = 0; cell < 81; ++cell)
        if (givens[cell] != '0') CHECK(game.value(cell) == static_cast<uint8_t>(givens[cell] - '0'));
}

int main()
{
    CHECK(sizeof(Sudoku::Game) <= 2048);
    testBundledPuzzleUniquenessAndLoading();
    testInputAndFixedCells();
    testNotesEntryAndErase();
    testCorrectnessAndCompletion();
    testRuleBasedIncorrect();
    testUndoRedoAndBranching();
    testHistoryCapacityAndImmutability();
    std::printf("%d checks, %d failures (Game: %u bytes)\n", checks, failures,
                static_cast<unsigned>(sizeof(Sudoku::Game)));
    return failures ? 1 : 0;
}
