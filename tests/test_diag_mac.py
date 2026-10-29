#!/usr/bin/env python3
"""Compile the production MAC diagnostics block for radio and radio-less targets."""
import pathlib
import subprocess
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
source = (ROOT / "components/my_diag/my_diag.cpp").read_text()
block = source[source.index("    uint8_t mac[6]"):source.index('    cJSON_AddStringToObject(root, "fw"')]
STUB = r'''
#include <cassert>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <string>
using esp_err_t=int;
constexpr int ESP_OK=0, ESP_MAC_WIFI_STA=0, ESP_MAC_BASE=5;
static bool fail=false;
static int errors=0, calls=0;
#define ESP_LOGE(...) (++errors)
int esp_read_mac(uint8_t *mac, int type) {
#if CONFIG_SOC_WIFI_SUPPORTED
    assert(type==ESP_MAC_WIFI_STA);
#else
    assert(type==ESP_MAC_BASE);
#endif
    ++calls;
    const uint8_t bytes[]={0x80,0xF1,0xB2,0xD7,0x8F,0x11};
    std::memcpy(mac,bytes,6);
    return fail ? -1 : ESP_OK;
}
std::string report() {
'''
TEST = r'''
    return macs;
}
int main() {
    for (int i=0;i<100;++i) assert(report()=="80:F1:B2:D7:8F:11");
    assert(calls==100 && errors==0);
    fail=true;
    for (int i=0;i<100;++i) assert(report().empty());
    assert(errors==1); // Unexpected read failure stays visible without flooding the log.
}
'''
with tempfile.TemporaryDirectory(prefix="otr-diag-mac-") as directory:
    work = pathlib.Path(directory)
    test = work / "test.cpp"
    test.write_text(STUB + block + TEST)
    for wifi in [0, 1]:
        binary = work / f"wifi-{wifi}"
        subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
                        f"-DCONFIG_SOC_WIFI_SUPPORTED={wifi}", str(test), "-o", str(binary)], check=True)
        subprocess.run([str(binary)], check=True)
print("Diagnostics MAC: native Wi-Fi and radio-less host selection, errors and repeated polling passed")
