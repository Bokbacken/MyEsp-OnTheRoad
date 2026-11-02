# Sudoku engine and puzzle bank

## Playing on the device

Open **Pastimes → Sudoku** (Swedish: **Tidsfördriv → Sudoku**) and select
Easy, Medium, Hard or Extreme. Select a digit in the 3×3 keypad, then touch a
board cell. The selected button and matching board digits are highlighted.
The pencil toggles positional notes; matching notes are highlighted too.
The left tools are undo, redo and check; the right tools are pencil, erase
and pause. Check overlays red crosses on incorrect entered digits and ignores
notes. It marks duplicates in the same row, column or 3×3 box, including both
cells in a conflicting pair; it does not compare entries with the solution.
A keypad digit is grayed out when all nine copies are on the board.
Tapping a cell with the currently selected digit toggles that entry off;
replacing it with a different digit asks for confirmation. Solving the puzzle
shows a randomly selected, animated encouragement over the board. Pause hides
the board and stops the elapsed timer, including time since the last timer
tick. If saving fails when leaving, the game stays open so the warning remains
visible and the save can be retried.
When placing a digit, matching notes are removed from its row, column and
3×3 box. After the floating completion cheer, choose the next puzzle or return
to the Sudoku difficulty list. Puzzle number, difficulty and elapsed time are
shown in a larger status line above the board.

The current puzzle, notes, elapsed time and completed count per difficulty
are saved in NVS every 30 seconds, when pausing, leaving or finishing. Power
loss can therefore discard the last 30 seconds; undo history is not persisted.
Only one unfinished puzzle is saved, with confirmation before replacing it.
Use **Continue saved puzzle** to resume after returning or restarting.
Finishing unlocks the next puzzle in that difficulty. There is no hint solver
or automatic candidate elimination.

Portrait and landscape layouts adapt to S3/P4 screen sizes. The animated home
icon is a miniature grid where a pencil note becomes a digit and other
digits appear. It follows the existing Administration animation setting.
Hardware touch/appearance checks on both boards remain pending.

## Model

The Sudoku model is a UI-independent C++ engine in `main/sudoku_game.h` and
`main/sudoku_game.cpp`. It uses a classic 9×9 board with cell indices 0–80 and
digits 1–9. The four difficulties are `EASY`, `MEDIUM`, `HARD`, and `EXTREME`;
Extreme uses the source bank's `diabolical.txt` category.

The model supports fixed givens, digit placement, per-cell candidate notes,
erase, undo/redo, detecting duplicate placed digits in rows, columns and boxes,
and a completion predicate based on the stored solution. Rule checking does not
compare an individual entry to the solution. Notes are a nine-bit mask: bit
`digit - 1` represents that candidate (digits 1–9 map to bits 0–8). The model
does not implement pause state or UI selection; the caller can pause and resume
by retaining the `Game` instance. A `Game` has no dynamic allocations and
keeps a 128-action bounded undo/redo history. Restore saved values and notes
through the edit methods, then call `clearHistory()` so undo cannot enter the
prior session. Invalid inputs and edits to givens are rejected without
changing game state.

The first bundled block has 100 puzzles per difficulty (400 total) stored as
81-character givens strings in flash; solutions are derived when a puzzle is
started. No puzzle is fetched from the network at runtime. SD delivery of
subsequent blocks is not implemented. Future blocks can be prepared offline:

```sh
git clone https://github.com/grantm/sudoku-exchange-puzzle-bank.git sudoku-exchange-puzzle-bank
git -C sudoku-exchange-puzzle-bank checkout d8c8ebaee0c08c412cfba96af1923dfa61c83317
python3 tools/import_sudoku.py --source-dir sudoku-exchange-puzzle-bank --block 1
```

Block numbers are zero-based. The utility takes the next 100 records per
difficulty in the original file order and writes `main/sudoku_puzzles.h` by
default; `--output PATH` can select another destination. It validates every
imported puzzle's unique solution before writing. The pinned source files are
`easy.txt`, `medium.txt`, `hard.txt`, and `diabolical.txt`; the last is
presented in the game as Extreme.

## Data source and license

The bundled puzzles are from Grant McLean's
[Sudoku Exchange Puzzle Bank](https://github.com/grantm/sudoku-exchange-puzzle-bank),
pinned to commit
[`d8c8ebaee0c08c412cfba96af1923dfa61c83317`](https://github.com/grantm/sudoku-exchange-puzzle-bank/commit/d8c8ebaee0c08c412cfba96af1923dfa61c83317).
The repository describes the data as public domain and includes the Unlicense
in `LICENSE.txt`. The bank was generated with QQWing and graded with Sukaku
Explainer; the original description states that puzzles have unique
solutions. This project also validates uniqueness during import and in the
host test suite.

## Host tests

```sh
g++ -std=c++17 -O2 -Wall -Wextra -Werror -Imain \
  tests/test_sudoku.cpp main/sudoku_game.cpp -o tests/test_sudoku
./tests/test_sudoku
python3 tests/test_sudoku_ui.py
```
