idf_component_register(
  SRCS "app_main.cpp" "app_config.cpp" "app_players.cpp" "app_web.cpp"
       "board_backlight.cpp" "board_display.cpp" "board_rgb.cpp" "board_touch.cpp"
       "board_audio.cpp" "board_sd.cpp" "app_web_files.cpp" "audio_synth.cpp" "sound_bank.cpp" "fs_path.cpp"
       "dice_cup.cpp" "game_live.cpp" "game_yatzy.cpp" "game_pig.cpp" "game_farkle.cpp" "game_scc.cpp"
       "ui_boot.cpp" "ui_clock.cpp" "ui_numpad.cpp" "ui_ota.cpp" "ui_settings.cpp" "ui_wifi.cpp" "ui_yatzy.cpp" "ui_adventure.cpp"
       "lang/lang.cpp" "lang/lang_sv.cpp" "lang/lang_en.cpp"
  INCLUDE_DIRS "." "lang"
  REQUIRES my_log my_storage my_time my_diag my_rtc my_wifi my_web my_ota my_lvgl my_ui
           otr_engine otr_story lvgl__lvgl espressif__led_strip espressif__cjson
           esp_lcd esp_driver_gpio esp_driver_ledc esp_driver_i2c esp_driver_spi esp_timer esp_app_format
           esp_driver_i2s esp_driver_sdmmc sdmmc fatfs vfs)

idf_build_get_property(project_ver PROJECT_VER)
target_compile_definitions(${COMPONENT_LIB} PRIVATE FW_VERSION="${project_ver}")
