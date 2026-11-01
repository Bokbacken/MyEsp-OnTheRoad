"""S3 file manager: memory-aware upload admission and audio invalidation scoped to audio folders."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
test = r'''
#include <cassert>
#include "audio_paths.h"
#include "upload_admission.h"
int main() {
    using namespace AudioPaths;
    // Owner case: folder upload into karaoke must not restart background music
    assert(affected("/sdcard/otr/karaoke/Abba") == NONE);
    assert(affected("/sdcard/otr/karaoke/Hit_Trax/Madness_Cardiac Arrest.mid") == NONE);
    assert(affected("/sdcard/otr/music/menu.wav") == MUSIC);
    assert(affected("/sdcard/otr/MUSIC/Menu.wav") == MUSIC);   // FAT is case-insensitive
    assert(affected("/sdcard/otr/music") == MUSIC);
    assert(affected("/sdcard/otr/music/") == MUSIC);
    assert(affected("/sdcard/otr/musicbox/x.wav") == NONE);
    assert(affected("/sdcard/otr/sounds/dice.wav") == SOUNDS);
    assert(affected("/sdcard/otr") == ALL);                     // Ancestor rename/delete
    assert(affected("/sdcard/otr/") == ALL);
    assert(affected("/sdcard") == ALL);
    assert(affected("/sdcard/ot") == NONE);
    assert(affected(nullptr) == ALL);

    using namespace UploadAdmission;
    constexpr size_t K = 1024;
    assert(admit(0, 10 * K));                 // One upload is always tried
    assert(!admit(1, 36 * K));                // Owner S3 after karaoke: internal 36 KB
    assert(admit(1, 66 * K));                 // Owner S3 normal: 66 KB
    assert(!admit(1, 51 * K) && admit(1, 52 * K));
    assert(!admit(4, 400 * K));               // Hard cap
    assert(suggestedSlots(36 * K) == 1);
    assert(suggestedSlots(66 * K) == 2);
    assert(suggestedSlots(300 * K) == 4);     // P4
    assert(!internalCache(66 * K, 32 * K));   // S3: write cache goes to PSRAM
    assert(internalCache(300 * K, 32 * K));   // P4: DMA-capable internal cache
}
'''
with tempfile.TemporaryDirectory(prefix="otr-upload-mem-") as tmp:
    path = Path(tmp)
    (path / "test.cpp").write_text(test)
    subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra", "-Werror", f"-I{ROOT / 'main'}",
                    str(path / "test.cpp"), "-o", str(path / "test")], check=True)
    subprocess.run([str(path / "test")], check=True)

source = (ROOT / "main/app_web_files.cpp").read_text()
mkdir = source.split("static esp_err_t handleMkdir", 1)[1].split("static esp_err_t", 1)[0]
assert "changed(" not in mkdir and "invalidateFiles" not in mkdir
assert "UploadAdmission::admit(" in source and "sendError(r, 429" in source
page = (ROOT / "main/web/files.html").read_text()
assert "x.status === 429" in page and "uploadSlots" in page
web = (ROOT / "components/my_web/my_web.cpp").read_text()
assert '"429 Too Many Requests"' in web and '"503 Service Unavailable"' in web
print("upload admission (S3/P4 memory), audio path scoping and mkdir without audio restart passed")
