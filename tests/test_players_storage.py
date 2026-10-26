#!/usr/bin/env python3
"""Riktade tester för profiler: verklig Players-kod, fil-I/O och injicerade fel."""
import pathlib
import re
import subprocess
import tempfile

root = pathlib.Path(__file__).resolve().parents[1]
startup = (root / "main/app_main.cpp").read_text()
# Guard against re-enabling destructive format-on-mount-failure at the actual call site.
mounts = re.findall(r'MyFat::mount\("ffat",\s*"/ffat",\s*(true|false)\)', startup)
assert mounts == ["false"], "FFat får inte autoformateras vid monteringsfel"
with tempfile.TemporaryDirectory(prefix="players-storage-") as tmp:
    binary = str(pathlib.Path(tmp) / "test")
    subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra", "-Itests/player_stubs", "-Imain",
                    "tests/test_players_storage.cpp",
                    "-Wl,--wrap=fwrite,--wrap=fflush,--wrap=fsync,--wrap=fclose,--wrap=rename,--wrap=stat,--wrap=fread",
                    "-o", binary], cwd=root, check=True)
    subprocess.run([binary], check=True)
print("Uppstartsskydd mot automatisk FFat-formatering: godkänt")
