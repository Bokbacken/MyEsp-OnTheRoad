#!/usr/bin/env python3
"""Check Supernova's actual media discovery and format preference."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / "main/ui_supernova.cpp").read_text()
start = source.index("bool endsWithExt(")
end = source.index("\nstd::vector<Song> scanSongs", start)
functions = source[start:end]
header = r'''
#include <algorithm>
#include <cassert>
#include <cstring>
#include <dirent.h>
#include <filesystem>
#include <fstream>
#include <string>
#include <sys/stat.h>
#include <vector>
struct Song { std::string folder, audio, video, lyrics; };
'''
test = r'''
int main(int argc, char **argv) {
    assert(argc == 2);
    std::string dir = argv[1];
    auto file = [&](const char *name) { std::ofstream(dir + "/" + name).put('x'); };
    file("song.txt");
    file("video.mp4");
    Song mp4;
    assert(findMedia(dir, mp4) && mp4.video == dir + "/video.mp4" && mp4.audio.empty());
    file("video.AVI");
    file("music.MP3");
    Song avi;
    assert(findMedia(dir, avi) && avi.video == dir + "/video.AVI" && avi.audio == dir + "/music.MP3");
    std::filesystem::remove(dir + "/video.AVI");
    Song fallback;
    assert(findMedia(dir, fallback) && fallback.video == dir + "/video.mp4");
    std::filesystem::remove(dir + "/song.txt");
    Song incomplete;
    assert(!findMedia(dir, incomplete));
}
'''

with tempfile.TemporaryDirectory(prefix="supernova-media-") as tmp:
    work = Path(tmp)
    cpp = work / "test.cpp"
    cpp.write_text(header + functions + test)
    binary = work / "test"
    subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
                    str(cpp), "-o", str(binary)], check=True)
    files = work / "song"
    files.mkdir()
    subprocess.run([str(binary), str(files)], check=True)
print("Supernova media discovery passed")
