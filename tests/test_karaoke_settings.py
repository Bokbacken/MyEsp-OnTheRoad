"""Build and run the pure-C++ karaoke .kcfg format tests."""
import pathlib
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
BUILD = pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else pathlib.Path("/tmp/karaoke-settings")
BUILD.mkdir(parents=True, exist_ok=True)
binary = BUILD / "test_kara_settings"
subprocess.run(
    [
        "g++",
        "-std=c++17",
        "-Wall",
        "-Wextra",
        "-Werror",
        "-I",
        str(ROOT / "main"),
        "-I",
        str(ROOT / "components/otr_midi/include"),
        str(ROOT / "tests/test_kara_settings.cpp"),
        str(ROOT / "main/kara_settings.cpp"),
        str(ROOT / "components/otr_midi/otr_midi.cpp"),
        str(ROOT / "components/otr_midi/otr_midi_gm.cpp"),
        "-o",
        str(binary),
    ],
    check=True,
)
subprocess.run([str(binary)], check=True)
print("Karaoke .kcfg and registry retention: passed")
