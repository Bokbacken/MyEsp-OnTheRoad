idf_component_register(SRCS "app_main.cpp" INCLUDE_DIRS "."
  REQUIRES my_log my_storage my_time my_diag my_wifi my_web my_ota lvgl__lvgl)
