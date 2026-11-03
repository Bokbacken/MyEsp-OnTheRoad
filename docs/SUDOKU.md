# Sudoku engine and puzzle bank

## Playing on the device

Open **Pastimes → Sudoku** (Swedish: **Tidsfördriv → Sudoku**) and select
Easy, Medium, Hard or Extreme. Select a digit in the 3×3 keypad, then touch a
board cell. The selected button and matching board digits are highlighted.
The pencil toggles positional notes; matching notes are highlighted too.
The left tools are undo, redo and check; the right tools are pencil, erase
and pause. Rule conflicts in the same row, column or 3×3 box get red crosses
immediately, including both cells in a conflicting pair. Check also marks
entered digits that do not match the solution and ignores notes. Taps on fixed
clues do nothing. In **Settings → Game → Sudoku**, choose a 0–8 px inactive
border on every cell edge (default 2 px); the red illustration previews the
centered active area and updates with the setting. Brief control hints appear
when selecting a digit, notes, check, erase, undo/redo or pause. They overlay
the game briefly instead of taking a permanent layout row; turn them off with
**Show Sudoku help hints** in the same settings section.
A keypad digit is grayed out when all nine copies are on the board.
Tapping a cell with the currently selected digit toggles that entry off;
replacing it with a different digit asks for confirmation. Solving the puzzle
shows a randomly selected, animated encouragement over the board for 3.7
seconds, followed by a prompt with Back on the left and Next on the right.
Pause hides the board and stops the elapsed timer, including time since the
last timer tick. If saving fails when leaving, the game stays open so the
warning remains visible and the save can be retried.
When placing a digit, matching notes are removed from its row, column and
3×3 box. Puzzle number, difficulty and elapsed time are shown in a larger status
line above the board. Undo and redo use curved back/forward arrows.

The current puzzle, notes, elapsed time and completed count per difficulty
are saved in NVS every 30 seconds, when pausing, leaving or finishing. Power
loss can therefore discard the last 30 seconds; undo history is not persisted.
Only one unfinished puzzle is saved, with confirmation before replacing it.
Use **Continue saved puzzle** to resume after returning or restarting.
**Reset progress** in **Settings → Game → Sudoku** asks for confirmation, then
removes the saved game and all four solved counts from Sudoku's NVS namespace.
It does not erase other settings, puzzle files or SD index caches.
Finishing unlocks the next puzzle in that difficulty. There is no hint solver
or automatic candidate elimination.

Portrait and landscape layouts adapt to S3/P4 screen sizes. The board uses the
available portrait width, and the keypad is shorter on compact displays. A
back icon is in the game header; its title alternates between Sudoku and the
current difficulty, puzzle number and time. The animated home icon is a
miniature grid where a pencil note becomes a digit and other digits appear. It
follows the existing Administration animation setting.
Hardware touch/appearance checks on both boards remain pending.

## Model

The Sudoku model is a UI-independent C++ engine in `main/sudoku_game.h` and
`main/sudoku_game.cpp`. It uses a classic 9×9 board with cell indices 0–80 and
digits 1–9. The four difficulties are `EASY`, `MEDIUM`, `HARD`, and `EXTREME`;
Extreme uses the source bank's `diabolical.txt` category.

The model supports fixed givens, digit placement, per-cell candidate notes,
erase, undo/redo, detecting duplicate placed digits in rows, columns and boxes,
and a completion predicate based on the stored solution. `incorrect()` detects
duplicate entries by Sudoku rules; `matchesSolution()` compares a placed digit
with the stored solution. Notes are a nine-bit mask: bit
`digit - 1` represents that candidate (digits 1–9 map to bits 0–8). The model
does not implement pause state or UI selection; the caller can pause and resume
by retaining the `Game` instance. A `Game` has no dynamic allocations and
keeps a 128-action bounded undo/redo history. Restore saved values and notes
through the edit methods, then call `clearHistory()` so undo cannot enter the
prior session. Invalid inputs and edits to givens are rejected without
changing game state.

The first bundled block has 100 puzzles per difficulty (400 total) stored as
81-character givens strings in flash; solutions are derived when a puzzle is
started. Additional puzzles are read from the SD card, in source-file order,
after the bundled block. Copy the original bank files into `/sdcard/otr/sudoku/`:

| Difficulty | File |
|---|---|
| Easy | `easy.txt` |
| Medium | `medium.txt` |
| Hard | `hard.txt` |
| Extreme | `extreme.txt` or `diabolical.txt` |

The selector shows the total number of available puzzles (bundled plus SD).
Valid-record counts and block offsets are cached beside each source as an
`.sidx` file. The cache is reused while the source file's size and modification
time are unchanged; only changed or uncached files need a full scan. The
generated `.sidx` files can be deleted safely and are rebuilt when needed.
When a file needs a full scan, a popup explains that checking new or changed
large files can take several minutes and remains visible until acknowledged
with OK. It appears only when a source file has no matching valid index cache,
such as when files are new or changed. The selector continues to show solved
progress out of the first 100 while the scan runs; the available total updates
when indexing finishes. With no source files present, no background scan task
is started. Puzzles are loaded one block of 100 at a time as the player
completes each block; no network access is used. If a source file is missing,
that difficulty remains limited to its 100 bundled puzzles. The source files
can be downloaded from [Grant McLean's Sudoku
Exchange Puzzle Bank](https://github.com/grantm/sudoku-exchange-puzzle-bank).

Future flash blocks can still be prepared offline:

```sh
git clone https://github.com/grantm/sudoku-exchange-puzzle-bank.git sudoku-exchange-puzzle-bank
git -C sudoku-exchange-puzzle-bank checkout d8c8ebaee0c08c412cfba96af1923dfa61c83317
python3 tools/import_sudoku.py --source-dir sudoku-exchange-puzzle-bank --block 1
```

Block numbers are zero-based. The utility takes the next 100 records per
difficulty in the original file order and writes `main/sudoku_puzzles.h` by
default; `--output PATH` can select another destination. It validates every
imported puzzle's unique solution before writing.

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
python3 tests/test_sudoku_sd.py
python3 tests/test_sudoku_ui.py
```
