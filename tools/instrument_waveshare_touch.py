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
static bool raw_touch_started;
static bool raw_touch_dumped;

static void raw_touch_record(int64_t start_us, esp_err_t result, const uint8_t *bytes)
{
    const int64_t end_us = esp_timer_get_time();
    if (raw_touch_dumped) return;
    if (!raw_touch_started) {
        if (result != ESP_OK || bytes[0] == 0xff ||
            bytes[1] < 1 || bytes[1] > 2) return;
        raw_touch_started = true;
    }
    raw_touch_sample_t *sample = &raw_touch_samples[raw_touch_count++];
    sample->time_us = start_us;
    sample->duration_us = end_us - start_us;
    sample->result = result;
    memcpy(sample->bytes, bytes, sizeof(sample->bytes));
    if (raw_touch_count != 512) return;
    raw_touch_dumped = true;
    ESP_LOGI("touch_raw", "BEGIN 512 buffered reads; first contact triggered capture; "
             "t_us=boot time, dt_us=I2C duration; no serial logging during capture");
    for (unsigned i = 0; i < raw_touch_count; ++i) {
        const raw_touch_sample_t *s = &raw_touch_samples[i];
        const uint8_t *b = s->bytes;
        ESP_LOGI("touch_raw", "n=%u t_us=%lld dt_us=%lld result=%s bytes="
                 "%02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X",
                 i, (long long)s->time_us, (long long)s->duration_us,
                 esp_err_to_name(s->result),
                 b[0], b[1], b[2], b[3], b[4], b[5], b[6],
                 b[7], b[8], b[9], b[10], b[11], b[12], b[13]);
    }
    ESP_LOGI("touch_raw", "END capture; reset to record again");
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


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("driver", type=Path)
    args = parser.parse_args()
    args.driver.write_text(instrument(args.driver.read_text()))
