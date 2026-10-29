#!/usr/bin/env bash
# Regenerates components/my_ui/fonts/my_font_raw_<size>.c (Montserrat Medium, ASCII + Latin-1,
# plus the LVGL FontAwesome symbols used by the UI). The public my_font_14/18/24/36 are runtime
# role fonts (components/my_ui/my_fonts.cpp) that point at one of these sizes per text-size level.
# Requires node/npx. Font sources are fetched from the LVGL repository (v9.2.0) when missing.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$ROOT/components/my_ui/fonts"
SRC="${FONT_SRC_DIR:-${TMPDIR:-/tmp}/otr-font-src}"
SIZES="${*:-12 14 16 18 20 22 24 28 32 36 40 44}"
SYMBOLS=61441,61448,61451,61452,61453,61457,61459,61461,61465,61468,61473,61478,61479,61480,61502,61507,61512,61515,61516,61517,61521,61522,61523,61524,61543,61544,61550,61552,61553,61556,61559,61560,61561,61563,61587,61589,61636,61637,61639,61641,61664,61671,61674,61683,61724,61732,61787,61931,62016,62017,62018,62019,62020,62087,62099,62212,62189,62810,63426,63650,61475,61596,62401

mkdir -p "$SRC"
for f in Montserrat-Medium.ttf "FontAwesome5-Solid+Brands+Regular.woff"; do
    if [ ! -s "$SRC/$f" ]; then
        curl -sSfL -o "$SRC/$f" \
            "https://raw.githubusercontent.com/lvgl/lvgl/v9.2.0/scripts/built_in_font/${f//+/%2B}"
    fi
done

cd "$SRC"
for size in $SIZES; do
    npx --yes lv_font_conv@1.5.3 --no-compress --no-prefilter --bpp 4 --size "$size" \
        --font Montserrat-Medium.ttf -r 0x20-0x7F,0xA0-0xFF,0x2022,0x2013,0x2014 \
        --font "FontAwesome5-Solid+Brands+Regular.woff" -r "$SYMBOLS" \
        --format lvgl --lv-include lvgl.h --force-fast-kern-format \
        -o "my_font_raw_$size.c"
    # Drop the absolute output path from the generated header comment.
    sed "s| -o my_font_raw_$size.c| -o my_font_raw_$size.c|; s|--force-fast-kern-format.*$|--force-fast-kern-format|" \
        "my_font_raw_$size.c" > "$OUT/my_font_raw_$size.c"
done
echo "Generated: $SIZES"
