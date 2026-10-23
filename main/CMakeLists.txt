idf_component_register(
  SRCS "app_main.cpp" "app_config.cpp" "app_players.cpp" "app_web.cpp"
       "board_backlight.cpp" "board_display.cpp" "board_rgb.cpp" "board_touch.cpp"
       "dice_cup.cpp" "game_live.cpp" "game_yatzy.cpp"
       "ui_boot.cpp" "ui_clock.cpp" "ui_ota.cpp" "ui_settings.cpp" "ui_wifi.cpp" "ui_yatzy.cpp"
  INCLUDE_DIRS "."
  REQUIRES my_log my_storage my_time my_diag my_rtc my_wifi my_web my_ota my_lvgl my_ui
           lvgl__lvgl espressif__led_strip espressif__cjson
           esp_lcd esp_driver_gpio esp_driver_ledc esp_driver_i2c esp_driver_spi esp_timer esp_app_format)

idf_build_get_property(project_ver PROJECT_VER)
target_compile_definitions(${COMPONENT_LIB} PRIVATE FW_VERSION="${project_ver}")
