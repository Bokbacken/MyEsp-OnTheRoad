"""Add a bounded RAM trace to an otherwise unmodified Waveshare touch driver."""
import argparse
from pathlib import Path


DECLARATION = "touch_data_t g_touch_data;\n"
HOOK = """        bsp_i2c_unlock();
        if (err != ESP_OK)"""
TRACE = r"""

// Capture first, print afterwards: no serial output during the measured reads.
#include "esp_timer.h"
#include <string.h>

typedef struct {
    int64_t time_us;
    int64_t duration_us;
    esp_err_t result;
    uint8_t bytes[14];
} raw_touch_sample_t;

static raw_touch_sample_t raw_touch_samples[512];
static unsigned raw_touch_count;
static unsigned raw_touch_next;

static void raw_touch_record(int64_t start_us, esp_err_t result, const uint8_t *bytes)
{
    const int64_t end_us = esp_timer_get_time();
    raw_touch_sample_t *sample = &raw_touch_samples[raw_touch_next];
    sample->time_us = start_us;
    sample->duration_us = end_us - start_us;
    sample->result = result;
    memcpy(sample->bytes, bytes, sizeof(sample->bytes));
    raw_touch_next = (raw_touch_next + 1) % 512;
    if (raw_touch_count < 512) ++raw_touch_count;
}

// Called by the drawing-screen clear button on the same LVGL task as reads.
void raw_touch_dump(void)
{
    ESP_LOGI("touch_raw", "BEGIN %u buffered reads; refresh requested dump; "
             "t_us=boot time, dt_us=I2C duration; no serial logging during capture",
             raw_touch_count);
    const unsigned oldest = raw_touch_count == 512 ? raw_touch_next : 0;
    for (unsigned i = 0; i < raw_touch_count; ++i) {
        const raw_touch_sample_t *s = &raw_touch_samples[(oldest + i) % 512];
        const uint8_t *b = s->bytes;
        ESP_LOGI("touch_raw", "n=%u t_us=%lld dt_us=%lld result=%s bytes="
                 "%02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X",
                 i, (long long)s->time_us, (long long)s->duration_us,
                 esp_err_to_name(s->result),
                 b[0], b[1], b[2], b[3], b[4], b[5], b[6],
                 b[7], b[8], b[9], b[10], b[11], b[12], b[13]);
    }
    raw_touch_count = raw_touch_next = 0;
    ESP_LOGI("touch_raw", "END capture; buffer cleared, recording continues");
}
"""


def instrument(source):
    if "raw_touch_record" in source:
        raise ValueError("Touch driver is already instrumented")
    if source.count(DECLARATION) != 1 or source.count(HOOK) != 1:
        raise ValueError("Unexpected upstream touch driver; refusing to patch")
    transaction = "        err = i2c_master_transmit_receive("
    if source.count(transaction) != 1:
        raise ValueError("Expected exactly one combined I2C transaction")
    source = source.replace(DECLARATION, DECLARATION + TRACE)
    source = source.replace(transaction, "        const int64_t raw_start_us = esp_timer_get_time();\n" + transaction)
    return source.replace(HOOK, """        bsp_i2c_unlock();
        raw_touch_record(raw_start_us, err, data);
        if (err != ESP_OK)""")


def instrument_drawing(source):
    marker = "void btn_clear_event_handler(lv_event_t *e)\n{"
    if "raw_touch_dump" in source:
        raise ValueError("Drawing screen is already instrumented")
    if source.count(marker) != 1:
        raise ValueError("Unexpected upstream drawing screen; refusing to patch")
    return source.replace(marker, """extern void raw_touch_dump(void);

""" + marker + """
    raw_touch_dump();""")


def instrument_cmake(source):
    marker = '                    REQUIRES "esp_lcd"'
    if source.count(marker) != 1 or "PRIV_REQUIRES" in source:
        raise ValueError("Unexpected upstream BSP dependencies; refusing to patch")
    return source.replace(marker, '                    PRIV_REQUIRES esp_timer\n' + marker)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("driver", type=Path)
    parser.add_argument("drawing", type=Path)
    args = parser.parse_args()
    cmake_path = args.driver.parent / "CMakeLists.txt"
    driver = instrument(args.driver.read_text())
    drawing = instrument_drawing(args.drawing.read_text())
    cmake = instrument_cmake(cmake_path.read_text())
    args.driver.write_text(driver)
    args.drawing.write_text(drawing)
    cmake_path.write_text(cmake)
