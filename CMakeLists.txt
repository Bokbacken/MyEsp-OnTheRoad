# MyEsp-OnTheRoad - ESP-IDF project (ESP32-S3, Heemol/LCDWIKI ES3C35P 3.5" ST77922).
cmake_minimum_required(VERSION 3.22)
set(PROJECT_VER "0.13.4")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(MyEsp_OnTheRoad)

# LVGL builtin heap (CONFIG_LV_MEM_SIZE) -> PSRAM .bss (needs CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY).
idf_component_get_property(lvgl_lib lvgl__lvgl COMPONENT_LIB)
target_compile_definitions(${lvgl_lib} PRIVATE "LV_ATTRIBUTE_LARGE_RAM_ARRAY=__attribute__((section(\".ext_ram.bss\")))")
