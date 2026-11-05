import importlib.util
from pathlib import Path
import unittest
import subprocess
import tempfile


spec = importlib.util.spec_from_file_location(
    "instrument", Path(__file__).resolve().parents[1] / "tools/instrument_waveshare_touch.py"
)
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class InstrumentTest(unittest.TestCase):
    def test_declares_timer_dependency(self):
        source = 'idf_component_register(SRCS ${SRC_FILES}\n                    REQUIRES "esp_lcd" "driver")'
        patched = module.instrument_cmake(source)
        self.assertIn("PRIV_REQUIRES esp_timer", patched)
        self.assertEqual(patched.replace("                    PRIV_REQUIRES esp_timer\n", ""), source)
        with self.assertRaises(ValueError):
            module.instrument_cmake(patched)
        with self.assertRaises(ValueError):
            module.instrument_cmake("")

    def test_only_adds_observation_around_transaction(self):
        source = """touch_data_t g_touch_data;
void read(void) {
        err = i2c_master_transmit_receive(dev_handle, read_cmd, 11, data, 14, pdMS_TO_TICKS(1000));
        bsp_i2c_unlock();
        if (err != ESP_OK) return;
}
"""
        patched = module.instrument(source)
        restored = patched.replace(module.TRACE, "").replace(
            "        const int64_t raw_start_us = esp_timer_get_time();\n", ""
        ).replace("        raw_touch_record(raw_start_us, err, data);\n", "")
        self.assertEqual(restored, source)
        self.assertLess(patched.index("bsp_i2c_unlock();"), patched.index("raw_touch_record(raw_start_us"))
        with self.assertRaises(ValueError):
            module.instrument(patched)

    def test_rejects_unexpected_upstream(self):
        for source in ("", module.DECLARATION, module.DECLARATION * 2 + module.HOOK):
            with self.assertRaises(ValueError):
                module.instrument(source)

    def test_drawing_patch_preserves_clear_handler(self):
        source = "void btn_clear_event_handler(lv_event_t *e)\n{\n    clear_canvas();\n}\n"
        patched = module.instrument_drawing(source)
        self.assertIn("raw_touch_dump();\n    clear_canvas();", patched)
        self.assertEqual(patched.replace("extern void raw_touch_dump(void);\n\n", "").replace(
            "\n    raw_touch_dump();", ""), source)
        with self.assertRaises(ValueError):
            module.instrument_drawing(patched)
        with self.assertRaises(ValueError):
            module.instrument_drawing("")

    def test_ring_capture_only_logs_on_refresh_and_rearms(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "esp_timer.h").write_text("int64_t esp_timer_get_time(void);\n")
            source = r"""
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <assert.h>
typedef int esp_err_t;
#define ESP_OK 0
static unsigned log_lines;
static int64_t now;
int64_t esp_timer_get_time(void) { return ++now; }
const char *esp_err_to_name(int result) { return result ? "error" : "ok"; }
#define ESP_LOGI(tag, ...) do { ++log_lines; if (0) printf(__VA_ARGS__); } while (0)
""" + module.TRACE + r"""
int main(void) {
    uint8_t bytes[14] = {0};
    raw_touch_record(0, ESP_OK, bytes);
    assert(raw_touch_count == 1 && log_lines == 0);
    bytes[1] = 1;
    for (unsigned i = 0; i < 1024; ++i) {
        raw_touch_record(now, i == 1 ? 3 : ESP_OK, bytes);
        assert(log_lines == 0);
    }
    assert(raw_touch_count == 512 && raw_touch_next == 1);
    assert(raw_touch_samples[raw_touch_next].time_us == 513);
    assert(raw_touch_samples[0].time_us == 1024);
    assert(raw_touch_samples[0].duration_us == 1);
    raw_touch_dump();
    assert(log_lines == 514 && raw_touch_count == 0 && raw_touch_next == 0);
    raw_touch_record(now, 3, bytes);
    assert(raw_touch_samples[0].result == 3);
    assert(log_lines == 514 && raw_touch_count == 1);
    raw_touch_record(now, ESP_OK, bytes);
    raw_touch_dump();
    assert(log_lines == 518 && raw_touch_count == 0);
    raw_touch_dump();
    assert(log_lines == 520 && raw_touch_count == 0);
    return 0;
}
"""
            binary = root / "trace-test"
            subprocess.run(
                ["gcc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-fsanitize=undefined",
                 "-I", str(root), "-x", "c", "-", "-o", str(binary)],
                input=source, text=True, check=True,
            )
            subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    unittest.main()
