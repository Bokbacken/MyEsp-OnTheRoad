#!/usr/bin/env python3
"""Exercise production OTA streaming with IDF doubles, without bulk erase."""
import pathlib
import subprocess
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
p4 = (ROOT / "sdkconfig.defaults.esp32p4").read_text().splitlines()
assert "CONFIG_LCD_DSI_ISR_CACHE_SAFE=y" in p4
assert "CONFIG_LCD_DSI_ISR_CACHE_SAFE=y" not in (
    ROOT / "sdkconfig.defaults.esp32s3"
).read_text().splitlines()

STUB = r'''
#pragma once
#include <cassert>
#include <cstdint>
#include <cstring>
using esp_err_t=int;
using esp_ota_handle_t=uint32_t;
using esp_ota_img_states_t=int;
constexpr int ESP_OK=0, ESP_FAIL=-1, ESP_ERR_OTA_VALIDATE_FAILED=-2;
constexpr int ESP_OTA_IMG_PENDING_VERIFY=1, ESP_IMAGE_HEADER_MAGIC=0xE9;
constexpr uint32_t OTA_WITH_SEQUENTIAL_WRITES=0xFFFFFFFE, OTA_SIZE_UNKNOWN=0xFFFFFFFF;
using portMUX_TYPE=int;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(mux) ((void)(mux))
#define portEXIT_CRITICAL(mux) ((void)(mux))
#define ESP_LOGE(...) ((void)0)
inline const char *esp_err_to_name(int) { return "injected failure"; }
namespace MySys { inline uint32_t now=0; inline uint32_t millis() { return now; } }
namespace MyLog { template<class... T> void event(T...) {} }
struct esp_partition_t { uint32_t size; const char *label; };
inline esp_partition_t target={5*1024*1024,"app1"};
inline int begins=0, writes=0, aborts=0, selections=0;
inline bool failBegin=false, failWrite=false, failEnd=false;
inline const esp_partition_t *esp_ota_get_next_update_partition(const void *) { return &target; }
inline const esp_partition_t *esp_ota_get_running_partition() { return &target; }
inline const esp_partition_t *esp_ota_get_last_invalid_partition() { return nullptr; }
inline int esp_ota_begin(const esp_partition_t *p, size_t size, esp_ota_handle_t *h) {
    assert(p==&target && size==OTA_WITH_SEQUENTIAL_WRITES);
    ++begins; if (failBegin) return ESP_FAIL; *h=1; return ESP_OK;
}
inline int esp_ota_write(esp_ota_handle_t h, const void *, size_t) {
    assert(h==1); ++writes; return failWrite ? ESP_FAIL : ESP_OK;
}
inline int esp_ota_end(esp_ota_handle_t h) {
    assert(h==1); return failEnd ? ESP_ERR_OTA_VALIDATE_FAILED : ESP_OK;
}
inline int esp_ota_abort(esp_ota_handle_t h) { assert(h==1); ++aborts; return ESP_OK; }
inline int esp_ota_set_boot_partition(const esp_partition_t *p) {
    assert(p==&target); ++selections; return ESP_OK;
}
inline int esp_ota_get_state_partition(const esp_partition_t *, int *) { return ESP_FAIL; }
inline int esp_ota_mark_app_valid_cancel_rollback() { return ESP_OK; }
'''

TEST = r'''
#include "my_ota.h"
int main() {
    const uint8_t bytes[]={0xE9,1,2,3}, invalid[]={0,1,2,3};
    assert(!MyOta::begin(target.size+1) && begins==0);
    failBegin=true;
    assert(!MyOta::begin(8) && !MyOta::busy());
    failBegin=false;
    for (uint32_t total : {8u, 0u}) {
        assert(MyOta::begin(total) && MyOta::busy());
        assert(MyOta::state().written==0 && MyOta::state().total==total);
        assert(MyOta::write(bytes,4) && MyOta::state().written==4);
        assert(MyOta::write(bytes,4) && MyOta::state().written==8);
        assert(MyOta::end() && !MyOta::busy());
    }
    assert(selections==2);
    assert(MyOta::begin(4));
    int oldWrites=writes;
    assert(!MyOta::write(invalid,4) && writes==oldWrites && aborts==1);
    assert(MyOta::begin(4));
    failWrite=true;
    assert(!MyOta::write(bytes,4) && aborts==2 && !MyOta::busy());
    failWrite=false;
    assert(MyOta::begin(4) && MyOta::write(bytes,4));
    failEnd=true; // IDF rejects wrong-chip/corrupt images here.
    assert(!MyOta::end() && selections==2);
    assert(MyOta::state().phase==MyOta::Phase::FAILED);
    failEnd=false;
    assert(MyOta::begin(0));
    MySys::now+=20001;
    MyOta::checkStale();
    assert(!MyOta::busy() && aborts==3 && selections==2);
}
'''

with tempfile.TemporaryDirectory(prefix="otr-ota-stream-") as directory:
    work = pathlib.Path(directory)
    (work / "stubs.h").write_text(STUB)
    for header in ["esp_app_format.h", "esp_log.h", "esp_ota_ops.h",
                   "freertos/FreeRTOS.h", "my_log.h", "my_sys.h"]:
        path = work / header
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text('#include "stubs.h"\n')
    (work / "test.cpp").write_text('#include "stubs.h"\n#include <initializer_list>\n' + TEST)
    binary = work / "test"
    subprocess.run([
        "g++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
        f"-I{work}", f"-I{ROOT / 'components/my_ota/include'}",
        str(ROOT / "components/my_ota/my_ota.cpp"), str(work / "test.cpp"),
        "-o", str(binary),
    ], check=True)
    subprocess.run([str(binary)], check=True)
print("OTA: sequential erase, progress, size/magic guards, errors, validation and timeout passed")
