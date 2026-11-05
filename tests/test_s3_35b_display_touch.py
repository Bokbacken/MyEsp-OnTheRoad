#!/usr/bin/env python3
"""Compile/run the native S3 3.5B drivers against bounded host hardware mocks."""
from pathlib import Path
import re
import subprocess


ROOT = Path(__file__).resolve().parents[1]
TESTS = ROOT / "tests"
TEMPLATE = (TESTS / "test_s3_35b_display_touch.cpp").read_text()


def main():
    cases = {
        "display": (
            "board_display_s3_35b.cpp",
            ["normal", "missing-power", "failed-power", "memory-frame", "memory-dma", "semaphore",
             "spi", "io", "init-command", "dma-timeout", "lvgl",
             "runtime-timeout", "runtime-queue", "column-error", "bounds", "sleep-errors",
             "recovery", "recovery-reset-fail", "recovery-command-fail",
             "recovery-dma-fail", "recovery-after-task"],
        ),
        "touch": (
            "board_touch_axs15231b.cpp",
            ["normal", "idle-first", "idle-first-57", "idle-first-02", "missing-power", "failed-power", "missing-lvgl", "add-device",
             "bad-first-report", "startup-corrupt", "startup-stuck", "recovery-failure",
             "startup-recovery", "command-failure", "pointer"],
        ),
        "touch_diag": (
            "board_touch_axs15231b.cpp",
            ["diagnostics", "idle-first-57", "idle-first-02", "startup-recovery",
             "startup-corrupt", "startup-stuck", "command-failure"],
        ),
    }
    count = 0
    for kind, (source, scenarios) in cases.items():
        body = re.sub(r"^#include .*?$", "", (ROOT / "main" / source).read_text(), flags=re.M)
        code = TEMPLATE.replace("// DRIVER_UNDER_TEST", body)
        binary = TESTS / f".s3_35b_{kind}_host"
        try:
            subprocess.run(
                ["g++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
                 "-fsanitize=undefined", f"-DTEST_{'TOUCH' if kind == 'touch_diag' else kind.upper()}",
                 *(["-DCONFIG_OTR_S3_TOUCH_DIAGNOSTICS=1"] if kind == "touch_diag" else []),
                 "-I", str(TESTS), "-x", "c++", "-", "-o", str(binary)],
                input=code, text=True, check=True, cwd=ROOT,
            )
            for scenario in scenarios:
                subprocess.run([str(binary), scenario], check=True, cwd=ROOT)
                count += 1
        finally:
            binary.unlink(missing_ok=True)
    indev = (ROOT / "managed_components/lvgl__lvgl/src/indev/lv_indev.c").read_text()
    send_event_start = indev.index("static lv_result_t send_event(lv_event_code_t code, void * param)\n{")
    send_event_end = indev.index("\n}\n", send_event_start) + 3
    send_event = indev[send_event_start:send_event_end]
    assert send_event.index("LV_EVENT_CLICKED") < send_event.index("lv_indev_send_event")
    assert send_event.index("indev_stop_processing_check") < send_event.index("lv_obj_send_event")
    print(f"S3 3.5B display/touch: {count} host scenarios passed")


if __name__ == "__main__":
    main()
