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
struct Media { std::string folder, audio, video, lyrics; };
'''
test = r'''
int main(int argc, char **argv) {
    assert(argc == 2);
    std::string dir = argv[1];
    auto file = [&](const char *name) { std::ofstream(dir + "/" + name).put('x'); };
    file("song.txt");
    file("video.mp4");
    Media mp4;
    assert(findMedia(dir, mp4) && mp4.video == dir + "/video.mp4" && mp4.audio.empty());
    file("video.AVI");
    file("music.MP3");
    Media avi;
    assert(findMedia(dir, avi) && avi.video == dir + "/video.AVI" && avi.audio == dir + "/music.MP3");
    std::filesystem::remove(dir + "/video.AVI");
    Media fallback;
    assert(findMedia(dir, fallback) && fallback.video == dir + "/video.mp4");
    file("music [Kar].mp3");
    file("music [Voc].mp3");
    Media variants;
    assert(findMedia(dir, variants) && variants.audio == dir + "/music.MP3" &&
           variants.instrumental == dir + "/music [Kar].mp3");
    std::filesystem::remove(dir + "/music [Kar].mp3");
    std::filesystem::remove(dir + "/music [Voc].mp3");
    std::filesystem::remove(dir + "/song.txt");
    Media incomplete;
    assert(!findMedia(dir, incomplete));
}
'''

with tempfile.TemporaryDirectory(prefix="supernova-media-") as tmp:
    work = Path(tmp)
    cpp = work / "test.cpp"
    cpp.write_text(header + functions + test)
    binary = work / "test"
    subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
                    "-Imain", "-Icomponents", str(cpp), str(root / "main/supernova_index.cpp"), "-o", str(binary)], check=True)
    files = work / "song"
    files.mkdir()
    subprocess.run([str(binary), str(files)], check=True)
print("Supernova media discovery passed")
