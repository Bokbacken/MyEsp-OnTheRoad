# MyEsp-OnTheRoad - ESP-IDF project. Boards: ES3C35P (esp32s3), Waveshare P4 4.3" (esp32p4).
cmake_minimum_required(VERSION 3.22)
set(PROJECT_VER "0.23.13")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
# Managed components differ per target (ESP-Hosted on P4), so keep one lock file per target.
if(NOT IDF_TARGET)
  message(FATAL_ERROR "IDF_TARGET is not set - run `idf.py set-target esp32s3|esp32p4` first")
endif()
idf_build_set_property(DEPENDENCIES_LOCK "${CMAKE_CURRENT_LIST_DIR}/dependencies.lock.${IDF_TARGET}")
message(STATUS "Dependencies lock: dependencies.lock.${IDF_TARGET}")
project(MyEsp_OnTheRoad)

# LVGL builtin heap (CONFIG_LV_MEM_SIZE) -> PSRAM .bss (needs CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY).
idf_component_get_property(lvgl_lib lvgl__lvgl COMPONENT_LIB)
target_compile_definitions(${lvgl_lib} PRIVATE "LV_ATTRIBUTE_LARGE_RAM_ARRAY=__attribute__((section(\".ext_ram.bss\")))")
