# MyEsp-OnTheRoad - ESP-IDF project. Boards: ES3C35P (esp32s3), Waveshare P4 4.3" (esp32p4).
cmake_minimum_required(VERSION 3.22)
set(PROJECT_VER "0.22.0")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
# Managed components differ per target (ESP-Hosted on P4), so keep one lock file per target.
if(IDF_TARGET)
  set(DEPENDENCIES_LOCK "${CMAKE_CURRENT_LIST_DIR}/dependencies.lock.${IDF_TARGET}")
endif()
project(MyEsp_OnTheRoad)

# LVGL builtin heap (CONFIG_LV_MEM_SIZE) -> PSRAM .bss (needs CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY).
idf_component_get_property(lvgl_lib lvgl__lvgl COMPONENT_LIB)
target_compile_definitions(${lvgl_lib} PRIVATE "LV_ATTRIBUTE_LARGE_RAM_ARRAY=__attribute__((section(\".ext_ram.bss\")))")
