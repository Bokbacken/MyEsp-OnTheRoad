#!/usr/bin/env python3
"""Check the boot and Wi-Fi connect layouts on both panels for every text size.

Compiles the production layout functions (main/ui_boot.cpp, main/ui_wifi.cpp) without LVGL and
feeds them the real line heights of the generated fonts for each MyFonts::TextSize level.
Run: python3 tests/test_text_size_layout.py
"""
import pathlib
import re
import subprocess
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]


def section(path, start, end):
    text = (ROOT / path).read_text()
    return text[text.index(start):text.index(end)]


line_heights = {}
for font in (ROOT / "components/my_ui/fonts").glob("my_font_raw_*.c"):
    px = int(re.search(r"my_font_raw_(\d+)", font.name).group(1))
    line_heights[px] = int(re.search(r"\.line_height = (\d+),", font.read_text()).group(1))
fonts_h = (ROOT / "components/my_ui/include/my_fonts.h").read_text()
role_px = [[int(v) for v in row.split(",")]
           for row in re.findall(r"\{(\d+, \d+, \d+, \d+)\}", fonts_h)]
assert len(role_px) == 4, role_px
for row in role_px:
    for px in row:
        assert px in line_heights, f"font size {px} is not generated"
assert role_px[1] == [14, 18, 24, 36], "NORMAL must keep the original sizes"

boot = section("main/ui_boot.cpp", "Layout layout(", "void show()")
conn = section("main/ui_wifi.cpp", "ConnLayout connLayout(", "void showConnecting(")
boot_h = section("main/ui_boot.h", "constexpr int32_t LIST_PAD", "void show();")
conn_h = section("main/ui_wifi.h", "struct ConnLayout", "ConnLayout connLayout(")
roles = ",\n".join("{%s}" % ", ".join(str(line_heights[px]) for px in row) for row in role_px)

code = r'''
#include <cassert>
#include <cstdint>
#include <cstdio>
#define LV_MIN(a, b) ((a) < (b) ? (a) : (b))
#define LV_MAX(a, b) ((a) > (b) ? (a) : (b))
#define LV_CLAMP(lo, v, hi) LV_MIN(LV_MAX(lo, v), hi)
namespace UiBoot {
enum class Item : int { DISP = 0, TOUCH, PSRAM, I2C, RTC, STORAGE, WIFI, WEB, COUNT };
@BOOT_H@
@BOOT@
}
namespace UiWifi {
@CONN_H@
@CONN@
}
// Line heights of the 14/18/24/36 roles per text size.
static const int32_t LINE[4][4] = {
@ROLES@
};

static void check(int32_t w, int32_t h, int size)
{
    const int32_t small = LINE[size][0], body = LINE[size][1], title = LINE[size][2], big = LINE[size][3];
    const UiBoot::Layout b = UiBoot::layout(w, h, big, small, body);
    if (b.showLogo) assert(b.titleY >= b.logoY + b.logo);
    assert(b.versionY >= b.titleY + big - 5);
    assert(b.barY >= b.versionY + small);
    assert(b.listY >= b.barY + b.barH);
    assert(b.rowH >= 2 * small - 3);
    assert(b.listH == b.rowH * (int32_t)UiBoot::Item::COUNT + UiBoot::LIST_PAD);
    assert(b.listY + b.listH <= b.statusY);
    assert(b.statusY + small <= h);
    assert(b.cardW <= w && b.barW <= w);
    if (b.bigName) assert(b.rowH >= body + small);
    // The checklist fills the panel: less than one row of unused space above the status line.
    assert(b.statusY - (b.listY + b.listH) < b.rowH + 8 || b.rowH == b.listH / 8);

    const UiWifi::ConnLayout c = UiWifi::connLayout(w, h, title, body, small, big);
    assert(c.titleY >= 0);
    assert(c.ssidY >= c.titleY + title);
    assert(c.spinnerY >= c.ssidY + title);
    assert(c.iconY >= c.spinnerY && c.iconY + big <= c.spinnerY + c.spinner);
    assert(c.barY >= c.spinnerY + c.spinner);
    assert(c.timeY >= c.barY + c.barH);
    assert(c.msgY >= c.timeY + small);
    assert(c.msgBottom <= c.btnY);
    assert(c.btnY + c.btnH <= h);
    assert(c.btnH >= body + 20);
    assert(c.textW <= w && c.barW <= w && c.btnW <= w);
    std::printf("%dx%d size %d: boot rowH=%d logo=%d bigName=%d spare=%d | conn top=%d spare=%d\n", (int)w,
                (int)h, size, (int)b.rowH, (int)b.showLogo, (int)b.bigName,
                (int)(b.statusY - b.listY - b.listH), (int)c.titleY, (int)(c.btnY - c.msgBottom));
}

int main()
{
    for (int size = 0; size <= 2; size++) check(320, 480, size);   // S3: up to LARGE
    for (int size = 0; size <= 3; size++) check(480, 800, size);   // P4: up to XLARGE
    // S3 at the normal size keeps the original boot geometry.
    const UiBoot::Layout s3 = UiBoot::layout(320, 480, LINE[1][3], LINE[1][0], LINE[1][1]);
    assert(s3.showLogo && s3.logo == 64 && s3.cardW == 296 && s3.barW == 260 && s3.rowH == 33);
    assert(!s3.bigName);
    // P4 uses the larger name font and larger controls.
    const UiBoot::Layout p4 = UiBoot::layout(480, 800, LINE[1][3], LINE[1][0], LINE[1][1]);
    assert(p4.showLogo && p4.bigName && p4.logo == 96 && p4.rowH > 50);
    std::puts("text size layouts OK");
}
'''
code = (code.replace("@BOOT_H@", boot_h).replace("@BOOT@", boot).replace("@CONN_H@", conn_h)
        .replace("@CONN@", conn).replace("@ROLES@", roles))
# MyFonts::apply() with a stub lv_font_t: every role follows ROLE_PX and XLARGE is clamped on 320 px.
FONTS_TEST = r'''
#include <cassert>
#include <cstdio>
#include "my_fonts.h"
@RAW@
int main()
{
    using MyFonts::TextSize;
    assert(my_font_14.line_height == 14 && my_font_36.line_height == 36);   // Static default = NORMAL
    for (int s = 0; s < (int)TextSize::COUNT; s++) {
        assert(MyFonts::apply((TextSize)s, 480) == (TextSize)s);
        assert(MyFonts::current() == (TextSize)s);
        assert(my_font_14.line_height == MyFonts::ROLE_PX[s][0]);
        assert(my_font_18.line_height == MyFonts::ROLE_PX[s][1]);
        assert(my_font_24.line_height == MyFonts::ROLE_PX[s][2]);
        assert(my_font_36.line_height == MyFonts::ROLE_PX[s][3]);
    }
    assert(MyFonts::apply(TextSize::XLARGE, 320) == TextSize::LARGE);
    assert(my_font_18.line_height == MyFonts::ROLE_PX[2][1]);
    assert(MyFonts::apply((TextSize)9, 320) == TextSize::NORMAL);
    std::puts("font roles OK");
}
'''
raw = "\n".join(f"extern const lv_font_t my_font_raw_{px} = {{{px}}};" for px in sorted(line_heights))

with tempfile.TemporaryDirectory() as tmp:
    tmp = pathlib.Path(tmp)
    src = tmp / "test.cpp"
    src.write_text(code)
    exe = tmp / "test"
    subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra", "-Werror", str(src), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
    (tmp / "lvgl.h").write_text("#pragma once\nstruct lv_font_t { int line_height; };\n"
                                "#define LV_FONT_DECLARE(n) extern const lv_font_t n;\n")
    (tmp / "fonts.cpp").write_text(FONTS_TEST.replace("@RAW@", raw))
    subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-I", str(tmp),
                    "-I", str(ROOT / "components/my_ui/include"), str(tmp / "fonts.cpp"),
                    str(ROOT / "components/my_ui/my_fonts.cpp"), "-o", str(tmp / "fonts")], check=True)
    subprocess.run([str(tmp / "fonts")], check=True)
