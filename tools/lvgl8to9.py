#!/usr/bin/env python3
"""lvgl8to9.py - rename LVGL 8 identifiers to their LVGL 9 names in C/C++ sources.

The rename table is read from LVGL's own api_map headers (old -> new #defines), followed by a
few extra renames that LVGL handles with wrappers or type changes. Behaviour changes (event
target types, font format, display driver) still need manual review afterwards.

Usage: tools/lvgl8to9.py <lvgl_dir> file1.cpp [file2.h ...]   (edits in place)
"""
import pathlib
import re
import sys

EXTRA = {
    "lv_coord_t": "int32_t",
    "lv_mem_alloc": "lv_malloc",
    "lv_mem_free": "lv_free",
    "lv_mem_realloc": "lv_realloc",
    "lv_disp_t": "lv_display_t",
    "lv_obj_get_child_cnt": "lv_obj_get_child_count",
    "lv_task_handler": "lv_timer_handler",
    "LV_IMG_CF_TRUE_COLOR": "LV_COLOR_FORMAT_NATIVE",
    "lv_txt_get_size": "lv_text_get_size",
    "LV_TEXT_FLAG_NONE": "LV_TEXT_FLAG_NONE",
    "lv_event_get_current_target": "lv_event_get_current_target_obj",
    "lv_event_get_target": "lv_event_get_target_obj",
    "lv_bar_set_value": "lv_bar_set_value",
    "lv_color_make": "lv_color_make",
    "lv_obj_set_style_shadow_ofs_y": "lv_obj_set_style_shadow_offset_y",
    "lv_obj_set_style_shadow_ofs_x": "lv_obj_set_style_shadow_offset_x",
    "lv_obj_set_style_img_recolor": "lv_obj_set_style_image_recolor",
    "lv_obj_set_style_img_recolor_opa": "lv_obj_set_style_image_recolor_opa",
    "lv_obj_set_style_img_opa": "lv_obj_set_style_image_opa",
    "lv_obj_set_style_anim_time": "lv_obj_set_style_anim_duration",
    "lv_obj_set_style_transform_angle": "lv_obj_set_style_transform_rotation",
    "lv_obj_set_style_transform_zoom": "lv_obj_set_style_transform_scale",
    "lv_anim_set_playback_time": "lv_anim_set_reverse_duration",
    "lv_anim_set_playback_delay": "lv_anim_set_reverse_delay",
    "lv_anim_set_time": "lv_anim_set_duration",
}
DROP = set()


def load_map(lvgl: pathlib.Path) -> dict:
    table = {}
    for f in sorted((lvgl / "include/lvgl/api_map").glob("lv_api_map_*.h")):
        for m in re.finditer(r"^#define\s+([A-Za-z_]\w*)\s+([A-Za-z_]\w*)\s*$", f.read_text(), re.M):
            table[m.group(1)] = m.group(2)
    table.update({k: v for k, v in EXTRA.items() if k not in DROP})
    # Resolve chains (a -> b -> c).
    for k in list(table):
        seen = {k}
        v = table[k]
        while v in table and v not in seen:
            seen.add(v)
            v = table[v]
        table[k] = v
    return {k: v for k, v in table.items() if k != v}


def main() -> int:
    lvgl = pathlib.Path(sys.argv[1])
    table = load_map(lvgl)
    rx = re.compile(r"\b(" + "|".join(sorted(map(re.escape, table), key=len, reverse=True)) + r")\b")
    for name in sys.argv[2:]:
        p = pathlib.Path(name)
        src = p.read_text()
        out = rx.sub(lambda m: table[m.group(1)], src)
        if out != src:
            p.write_text(out)
            print(f"updated {p}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
