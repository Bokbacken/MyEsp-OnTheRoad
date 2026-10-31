"""Exercise the P4 50 MHz experiment's negotiation and actual-clock guards."""
import pathlib
import subprocess
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]

STUB = r"""
#pragma once
#include <cassert>
using esp_err_t = int;
constexpr int ESP_OK=0, ESP_FAIL=-1, ESP_ERR_NOT_SUPPORTED=1, ESP_ERR_INVALID_STATE=2;
constexpr int SDMMC_FREQ_HIGHSPEED=40000;
#define CONFIG_ESP_HOSTED_HOST_SDIO_CLK_KHZ 50000
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGE(...) ((void)0)
inline const char *esp_err_to_name(int) { return "error"; }
struct sdmmc_card_t {
    struct { int slot, max_freq_khz; } host;
    int max_freq_khz, real_freq_khz;
};
inline int hsResult=ESP_OK, clockResult=ESP_OK, actualClock=50000, clockCalls=0;
extern "C" int __wrap_sdmmc_io_enable_hs_mode(sdmmc_card_t *);
extern "C" int __wrap_sdmmc_init_host_frequency(sdmmc_card_t *);
"""

DRIVER = r"""
#include "stub.h"
extern "C" int sdmmc_io_enable_hs_mode(sdmmc_card_t *) { return hsResult; }
extern "C" int sdmmc_init_host_frequency(sdmmc_card_t *card) {
    ++clockCalls;
    card->real_freq_khz=actualClock;
    return clockResult;
}
"""

TEST = r"""
#include "stub.h"
int main() {
    sdmmc_card_t radio{{1, 50000}, 40000, 0};
    assert(__wrap_sdmmc_io_enable_hs_mode(&radio)==ESP_OK && radio.max_freq_khz==50000);
    assert(__wrap_sdmmc_init_host_frequency(&radio)==ESP_OK && radio.real_freq_khz==50000);
    // A normal SD memory card already negotiated 50 MHz without the SDIO wrapper.
    sdmmc_card_t memory{{0, 50000}, 50000, 0};
    assert(__wrap_sdmmc_init_host_frequency(&memory)==ESP_OK && memory.real_freq_khz==50000);
    radio.max_freq_khz=40000; hsResult=ESP_FAIL;
    assert(__wrap_sdmmc_io_enable_hs_mode(&radio)==ESP_FAIL && radio.max_freq_khz==40000);
    const int calls=clockCalls;
    assert(__wrap_sdmmc_init_host_frequency(&radio)==ESP_ERR_NOT_SUPPORTED && clockCalls==calls);
    hsResult=ESP_OK; radio.max_freq_khz=20000;
    assert(__wrap_sdmmc_io_enable_hs_mode(&radio)==ESP_OK && radio.max_freq_khz==20000);
    assert(__wrap_sdmmc_init_host_frequency(&radio)==ESP_ERR_NOT_SUPPORTED);
    radio.max_freq_khz=50000; actualClock=40000;
    assert(__wrap_sdmmc_init_host_frequency(&radio)==ESP_ERR_INVALID_STATE);
    actualClock=50000; clockResult=ESP_FAIL;
    assert(__wrap_sdmmc_init_host_frequency(&radio)==ESP_FAIL);
    // Nonexperimental host requests keep their negotiated clock.
    radio.host.max_freq_khz=40000; radio.max_freq_khz=40000; clockResult=ESP_OK;
    assert(__wrap_sdmmc_io_enable_hs_mode(&radio)==ESP_OK && radio.max_freq_khz==40000);
    actualClock=40000;
    assert(__wrap_sdmmc_init_host_frequency(&radio)==ESP_OK);
    memory.host.max_freq_khz=20000; memory.max_freq_khz=20000; actualClock=20000;
    assert(__wrap_sdmmc_init_host_frequency(&memory)==ESP_OK);
}
"""


def main():
    with tempfile.TemporaryDirectory(prefix="otr-sdclock-") as directory:
        work = pathlib.Path(directory)
        (work / "stub.h").write_text(STUB)
        for name in ("sdkconfig.h", "esp_log.h", "sdmmc_cmd.h"):
            (work / name).write_text('#include "stub.h"\n')
        source = work / "test.cpp"
        source.write_text(TEST)
        driver = work / "driver.cpp"
        driver.write_text(DRIVER)
        flags = [
            "g++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-Wno-unused-variable",
            f"-I{work}", f"-I{ROOT}",
        ]
        for name, path in (("wrapper", ROOT / "main/board_sd_clock.cpp"),
                           ("driver", driver), ("test", source)):
            subprocess.run([*flags, "-c", str(path), "-o", str(work / f"{name}.o")],
                           check=True)
        subprocess.run(["ar", "rcs", str(work / "libmain.a"), str(work / "wrapper.o")],
                       check=True)
        subprocess.run(["ar", "rcs", str(work / "libsdmmc.a"), str(work / "driver.o")],
                       check=True)
        link_flags = [
            "-Wl,--undefined=__wrap_sdmmc_io_enable_hs_mode",
            "-Wl,--undefined=__wrap_sdmmc_init_host_frequency",
            "-Wl,--wrap=sdmmc_io_enable_hs_mode",
            "-Wl,--wrap=sdmmc_init_host_frequency",
        ]
        binary = work / "test"
        subprocess.run([
            "g++", str(work / "test.o"), str(work / "libmain.a"),
            str(work / "libsdmmc.a"), *link_flags, "-o", str(binary),
        ], check=True)
        subprocess.run([str(binary)], check=True)
        # IDF main has no ordinary callers of the wrappers: retain them anyway.
        source.write_text("int main() { return 0; }\n")
        subprocess.run([
            *flags, str(source), str(work / "libmain.a"), str(work / "libsdmmc.a"),
            *link_flags, "-o", str(binary),
        ], check=True)
        symbols = subprocess.check_output(["nm", str(binary)], text=True)
        for name in ("sdmmc_io_enable_hs_mode", "sdmmc_init_host_frequency"):
            assert f" T __wrap_{name}" in symbols
    print("SD 50 MHz negotiation, actual clocks and errors: passed")


if __name__ == "__main__":
    main()
