#!/usr/bin/env python3
"""Exercise the production MyFat implementation after a failed boot mount."""
import pathlib
import re
import subprocess
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
source = (ROOT / "components/my_storage/my_storage.cpp").read_text()
fat = source[source.index("namespace MyFat {"):]
route = (ROOT / "main/app_web_files.cpp").read_text().split("static esp_err_t handleFsRepair", 1)[1].split("bool registerRoutes", 1)[0]
assert "q.adminAllowed()" in route and 'strcmp(root, "ffat")' in route
assert 'if (!MyFat::mounted()) return' not in route
startup = (ROOT / "main/app_main.cpp").read_text()
assert re.findall(r'MyFat::mount\("ffat",\s*"/ffat",\s*(true|false)\)', startup) == ["false"]

STUB = r'''
#pragma once
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
using nvs_handle_t=uint32_t;
using wl_handle_t=int;
using esp_err_t=int;
constexpr int WL_INVALID_HANDLE=-1, ESP_OK=0, ESP_FAIL=-1;
#define CONFIG_WL_SECTOR_SIZE 4096
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
#define ESP_LOGE(...) ((void)0)
inline const char *esp_err_to_name(int) { return "injected error"; }
namespace MyLog { template<class... T> void event(T...) {} }
struct esp_vfs_fat_mount_config_t { bool format_if_mount_failed; int max_files; int allocation_unit_size; };
inline bool diskValid=false, failFormat=false, failRemount=false;
inline int formats=0, mounts=0;
inline int esp_vfs_fat_spiflash_mount_rw_wl(const char *base, const char *label,
                                         const esp_vfs_fat_mount_config_t *c, int *wl) {
    assert(std::strcmp(base,"/ffat")==0 && std::strcmp(label,"ffat")==0);
    assert(!c->format_if_mount_failed && c->max_files==11 && c->allocation_unit_size==4096);
    ++mounts;
    if (!diskValid || failRemount) return ESP_FAIL;
    *wl=7; return ESP_OK;
}
inline int esp_vfs_fat_spiflash_format_rw_wl(const char *base, const char *label) {
    assert(std::strcmp(base,"/ffat")==0 && std::strcmp(label,"ffat")==0);
    ++formats; if (failFormat) return ESP_FAIL; diskValid=true; return ESP_OK;
}
inline int esp_vfs_fat_info(const char *, uint64_t *total, uint64_t *free) {
    *total=6000000; *free=5900000; return ESP_OK;
}
'''

TEST = r'''
int main(int argc, char **argv) {
    using namespace MyFat;
    const char *mode=argc>1 ? argv[1] : "";
    assert(!format() && formats==0); // No partition has been selected.
    assert(!mount("ffat","/ffat",false,11));
    assert(!mounted() && formats==0 && mounts==1); // Boot is observation-only.
    assert(std::strcmp(basePath(),"/ffat")==0);
    if (std::strcmp(mode,"formatfail")==0) {
        failFormat=true;
        assert(!format() && !mounted() && mounts==1);
        failFormat=false;
    }
    if (std::strcmp(mode,"remountfail")==0) {
        failRemount=true;
        assert(!format() && !mounted());
        failRemount=false;
    }
    assert(format() && mounted() && totalBytes()==6000000);
    const int oldMounts=mounts, oldFormats=formats;
    assert(format() && mounted()); // Preserve mounted-volume repair behavior.
    assert(mounts==oldMounts && formats==oldFormats+1);
}
'''

with tempfile.TemporaryDirectory(prefix="otr-flash-repair-") as directory:
    work = pathlib.Path(directory)
    (work / "stubs.h").write_text(STUB)
    (work / "nvs.h").write_text('#include "stubs.h"\n')
    (work / "test.cpp").write_text(
        '#include "stubs.h"\n#include "my_storage.h"\n'
        '#include <dirent.h>\n#include <sys/stat.h>\n#include <unistd.h>\n'
        'static const char *TAG="storage";\n' + fat + TEST
    )
    subprocess.run([
        "g++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-Wno-unused-variable",
        f"-I{work}", f"-I{ROOT / 'components/my_storage/include'}",
        str(work / "test.cpp"), "-o", str(work / "test"),
    ], check=True)
    for mode in ["ok", "formatfail", "remountfail"]:
        subprocess.run([str(work / "test"), mode], check=True)
print("FFat: failed boot mount, explicit repair, format/remount errors and admin guards passed")
