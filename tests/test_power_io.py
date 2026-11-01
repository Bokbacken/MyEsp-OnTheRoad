#!/usr/bin/env python3
"""Check production storage counters without changing partition IO results."""
import pathlib
import subprocess
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
HEADER = r'''
#pragma once
#include <stddef.h>
#include <stdint.h>
using esp_err_t = int;
constexpr int ESP_OK=0, ESP_PARTITION_TYPE_DATA=1, ESP_PARTITION_SUBTYPE_DATA_FAT=0x81;
struct esp_partition_t { int type, subtype; char label[17]; };
'''
TEST = r'''
#include "esp_partition.h"
#include "power_io.h"
#include <cassert>
#include <cstdio>
int result=0, calls=0;
extern "C" int __real_esp_partition_read(const esp_partition_t *, size_t, void *, size_t) { ++calls; return result; }
extern "C" int __real_esp_partition_write(const esp_partition_t *, size_t, const void *, size_t) { ++calls; return result; }
extern "C" int __wrap_esp_partition_read(const esp_partition_t *, size_t, void *, size_t);
extern "C" int __wrap_esp_partition_write(const esp_partition_t *, size_t, const void *, size_t);
int main() {
    esp_partition_t fat{1,0x81,"ffat"}, nvs{1,2,"nvs"};
    char data[8]{};
    assert(__wrap_esp_partition_read(&fat,0,data,8)==0);
    assert(__wrap_esp_partition_write(&fat,0,data,7)==0);
    assert(__wrap_esp_partition_write(&nvs,0,data,8)==0);
    result=-1;
    assert(__wrap_esp_partition_write(&fat,0,data,8)==-1);
    assert(calls==4);
    PowerIo::sdRead(512); PowerIo::sdWrite(1024);
    const auto c=PowerIo::counts();
    assert(c.flashRead==8 && c.flashWrite==7 && c.sdRead==512 && c.sdWrite==1024);
    puts("Power IO: FFat-only successful bytes counted; IO errors preserved");
}
'''
with tempfile.TemporaryDirectory(prefix="otr-power-io-") as directory:
    work = pathlib.Path(directory)
    (work / "esp_partition.h").write_text(HEADER)
    (work / "test.cpp").write_text(TEST)
    binary = work / "test"
    subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra", "-Werror", f"-I{work}",
                    f"-I{ROOT / 'main'}", str(ROOT / "main/power_io.cpp"),
                    str(work / "test.cpp"), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
