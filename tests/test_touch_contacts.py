#!/usr/bin/env python3
"""Exercise the production ST77922/GT911 cached contact paths with register mocks."""
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
code = r'''
#include <cassert>
#include <cstdio>
#include <cstring>
#include "board_touch.h"
#include "board_pins.h"

#define IRAM_ATTR
#define ESP_LOGI(tag, ...) ((void)(tag))
#define ESP_LOGW(tag, ...) ((void)(tag))
#define ESP_LOGE(tag, ...) ((void)(tag))
constexpr int ESP_ERR_INVALID_STATE = 2;
constexpr int GPIO_MODE_INPUT = 0, GPIO_MODE_OUTPUT = 1, GPIO_PULLUP_ENABLE = 1, GPIO_INTR_NEGEDGE = 2;
struct gpio_config_t { uint64_t pin_bit_mask = 0; int mode = 0, pull_up_en = 0, intr_type = 0; };
void gpio_config(const gpio_config_t *) {}
void gpio_set_level(int, int) {}
int gpio_isr_handler_add(int, void (*)(void *), void *) { return ESP_OK; }
int gpio_install_isr_service(int) { return ESP_OK; }
void vTaskDelay(int) {}
int pdMS_TO_TICKS(int n) { return n; }
namespace MySys { uint32_t now = 100; uint32_t millis() { return now; } }
namespace MyLvgl {
bool (*pointer)(int16_t *, int16_t *);
lv_indev_t device;
lv_indev_t *addPointer(bool (*fn)(int16_t *, int16_t *)) { pointer = fn; return &device; }
}
uint8_t registers[65536] = {};
bool failRead = false;
int transactions = 0;
size_t lastPointBytes = 0;
int i2c_new_master_bus(const i2c_master_bus_config_t *, void **out) { *out = registers; return ESP_OK; }
int i2c_master_bus_add_device(void *, const i2c_device_config_t *, void **out) { *out = registers; return ESP_OK; }
int i2c_master_probe(void *, uint16_t, int) { return ESP_FAIL; }
int i2c_master_transmit_receive(void *, const uint8_t *tx, size_t txSize, uint8_t *rx, size_t n, int) {
    assert(txSize == 2);
    ++transactions;
    if (failRead) return ESP_FAIL;
    const uint16_t address = uint16_t(tx[0] << 8) | tx[1];
    if (address == 0x814F) lastPointBytes = n;
    std::memcpy(rx, registers + address, n);
    return ESP_OK;
}
int i2c_master_transmit(void *, const uint8_t *tx, size_t n, int) {
    assert(n == 3);
    registers[uint16_t(tx[0] << 8) | tx[1]] = tx[2];
    return ESP_OK;
}
// DRIVER_UNDER_TEST
void point(int index, int x, int y) {
#ifdef CONFIG_OTR_HW_PROFILE_P4_43
    auto *p = registers + REG_POINT_0 + index * 8;
    p[0] = uint8_t(index); p[1] = uint8_t(x); p[2] = uint8_t(x >> 8);
    p[3] = uint8_t(y); p[4] = uint8_t(y >> 8);
#else
    auto *p = registers + REG_REPORT_0 + index * REPORT_BYTES;
    p[0] = uint8_t(0x80 | (x >> 8)); p[1] = uint8_t(x);
    p[2] = uint8_t(y >> 8); p[3] = uint8_t(y);
#endif
}
void count(int n) {
#ifdef CONFIG_OTR_HW_PROFILE_P4_43
    registers[REG_STATUS] = uint8_t(0x80 | n);
#else
    registers[REG_TOUCH_INFO] = n ? 8 : 0;
    registers[REG_MAX_TOUCHES] = uint8_t(n);
#endif
}
bool poll(int16_t &x, int16_t &y) {
    MySys::now += 20;
    return pointerRead(&x, &y);
}
int main() {
    g_dev = registers;
    g_info.maxX = LCD_H_RES; g_info.maxY = LCD_V_RES;
    Touch::setMultiTouch(true);
    count(2); point(0, 10, 30); point(1, 200, 400);
    int16_t x = 0, y = 0;
    assert(poll(x, y) && x == 10 && y == 30);
    auto sample = Touch::contacts();
    assert(sample.count == 2 && sample.sampledAt == MySys::now);
    assert(sample.points[0].x == 10 && sample.points[0].y == 30);
    assert(sample.points[1].x == 200 && sample.points[1].y == 400);
    const int reads = transactions;
    Touch::contacts();
    assert(transactions == reads);
#ifdef CONFIG_OTR_HW_PROFILE_P4_43
    assert(lastPointBytes == 16);
#endif
    count(1); point(0, 200, 400); // Remaining finger becomes point 0.
    assert(poll(x, y) && x == 200 && y == 400);
    assert(Touch::contacts().count == 1 && Touch::contacts().points[0].x == 200);
    Touch::setRotation(true);
    sample = Touch::contacts();
#ifdef CONFIG_OTR_HW_PROFILE_P4_43
    assert(sample.points[0].x == 200 && sample.points[0].y == 400);
#else
    assert(sample.points[0].x == LCD_H_RES - 1 - 200);
    assert(sample.points[0].y == LCD_V_RES - 1 - 400);
#endif
    Touch::setRotation(false);
    count(0); poll(x, y);
    assert(Touch::contacts().count == 0); // Raw contacts do not latch the menu's release debounce.
    count(2); point(0, 10, 30); point(1, 200, 400);
    poll(x, y);
    assert(Touch::contacts().count == 2);
    failRead = true;
    poll(x, y);
    assert(Touch::contacts().count == 0 && Touch::info().readErrors == 1);
    failRead = false;
    Touch::setMultiTouch(false);
    count(2); poll(x, y);
    assert(Touch::contacts().count == 0 && x == 10 && y == 30);
#ifdef CONFIG_OTR_HW_PROFILE_P4_43
    assert(lastPointBytes == 8); // Existing menus still read only the primary contact.
#endif
    puts("Native contact snapshots: two/one/released/error/rotation/opt-out passed");
}
'''

with tempfile.TemporaryDirectory(prefix="otr-contacts-") as tmp:
    work = Path(tmp)
    (work / "lvgl.h").write_text("#pragma once\nstruct lv_indev_t {};\n")
    (work / "sdkconfig.h").write_text("")
    for driver, defines in (
        ("board_touch.cpp", []),
        ("board_touch_gt911.cpp", ["-DCONFIG_OTR_HW_PROFILE_P4_43=1"]),
    ):
        body = re.sub(r"^#include .*?$", "", (ROOT / "main" / driver).read_text(), flags=re.M)
        test = work / "test.cpp"
        test.write_text(code.replace("// DRIVER_UNDER_TEST", body))
        binary = work / "test"
        subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
                        "-fsanitize=undefined", *defines, f"-I{work}",
                        f"-I{ROOT / 'main'}", f"-I{ROOT / 'tests/host_stubs'}",
                        str(test), "-o", str(binary)], check=True)
        subprocess.run([str(binary)], check=True)
