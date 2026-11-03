#!/usr/bin/env python3
import pathlib
import re
import subprocess
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
PUZZLES = re.findall(r'"([0-9]{81})"', (ROOT / "main/sudoku_puzzles.h").read_text())


def record(puzzle, rating="1.0"):
    return f"012345abcdef {puzzle} {rating}\n"


with tempfile.TemporaryDirectory(prefix="sudoku-sd-") as directory:
    root = pathlib.Path(directory)
    sudoku = root / "otr" / "sudoku"
    sudoku.mkdir(parents=True)
    easy_rows = [record(PUZZLES[0]), "not a puzzle record\n"]
    easy_rows.extend(record(PUZZLES[1], "2.5") for _ in range(100))
    easy_rows.append(record(PUZZLES[0]))
    (sudoku / "easy.txt").write_text("".join(easy_rows), encoding="ascii")
    (sudoku / "medium.txt").write_text(record(PUZZLES[100]), encoding="ascii")
    (sudoku / "diabolical.txt").write_text(record(PUZZLES[300]), encoding="ascii")
    binary = root / "test_sudoku_sd"
    subprocess.run(
        [
            "g++",
            "-std=c++17",
            "-O2",
            "-Wall",
            "-Wextra",
            "-Werror",
            f'-DSUDOKU_SD_ROOT="{root}"',
            f"-I{ROOT / 'main'}",
            str(ROOT / "tests/test_sudoku_sd.cpp"),
            str(ROOT / "main/sudoku_sd.cpp"),
            str(ROOT / "main/sudoku_game.cpp"),
            "-o",
            str(binary),
        ],
        check=True,
    )
    subprocess.run([str(binary)], check=True)
print("Sudoku SD bank tests passed")
