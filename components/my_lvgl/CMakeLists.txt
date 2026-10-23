idf_component_register(SRCS "my_lvgl.cpp"
                       INCLUDE_DIRS "include"
                       REQUIRES lvgl__lvgl
                       PRIV_REQUIRES esp_timer heap log freertos)
