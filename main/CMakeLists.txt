# Board-specific drivers behind the shared board_*.h APIs (profile is bound to the target in Kconfig).
if(CONFIG_OTR_HW_PROFILE_P4_43)
  set(board_srcs "board_display_p4.cpp" "board_touch_gt911.cpp" "host_ota.cpp")
  set(board_reqs esp_lcd esp_hw_support esp_hosted)
else()
  set(board_srcs "board_display.cpp" "board_touch.cpp")
  set(board_reqs esp_lcd esp_driver_spi)
endif()

idf_component_register(
  SRCS "app_main.cpp" "app_config.cpp" "app_players.cpp" "app_web.cpp" "fat_time.cpp"
       "board_backlight.cpp" "board_rgb.cpp" ${board_srcs}
       "board_audio.cpp" "board_battery.cpp" "board_sd.cpp" "app_web_files.cpp" "app_web_adventure_editor.cpp" "audio_synth.cpp" "sound_bank.cpp" "fs_path.cpp" "fs_upload.cpp"
       "dice_cup.cpp" "game_live.cpp" "adv_live.cpp" "adv_session.cpp" "adv_loader.cpp" "adv_validator.cpp" "adv_catalog.cpp" "adv_sd_session.cpp" "adv_resource_art.cpp" "game_yatzy.cpp" "game_pig.cpp" "game_farkle.cpp" "game_scc.cpp"
       "hw_profile.cpp" "ui_metrics.cpp" "ui_boot.cpp" "ui_clock.cpp" "ui_numpad.cpp" "ui_ota.cpp" "ui_screen_power.cpp" "ui_settings.cpp" "ui_wifi.cpp" "ui_yatzy.cpp" "ui_multiplayer.cpp" "ui_adventure.cpp" "ui_karaoke.cpp" "karaoke.cpp" "kara_stream.cpp" "kara_web.cpp" "kara_index.cpp" "kara_songs_web.cpp" "otr_log.cpp"
       "lang/lang.cpp" "lang/lang_sv.cpp" "lang/lang_en.cpp"
  INCLUDE_DIRS "." "lang"
  REQUIRES my_log my_storage my_time my_diag my_rtc my_wifi my_web my_ota my_lvgl my_ui
           otr_engine otr_story otr_midi otr_synth lvgl__lvgl espressif__led_strip espressif__cjson
           ${board_reqs} esp_driver_gpio esp_driver_ledc esp_driver_i2c esp_adc esp_timer esp_app_format
           esp_driver_i2s esp_driver_sdmmc esp_http_client espressif__mdns espressif__esp_websocket_client
           sdmmc fatfs vfs)

idf_build_get_property(project_ver PROJECT_VER)
target_compile_definitions(${COMPONENT_LIB} PRIVATE FW_VERSION="${project_ver}")
