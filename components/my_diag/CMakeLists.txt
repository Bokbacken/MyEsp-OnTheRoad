idf_component_register(SRCS "my_diag.cpp"
                       INCLUDE_DIRS "include"
                       REQUIRES espressif__cjson
                       PRIV_REQUIRES my_log my_storage esp_app_format espcoredump spi_flash esp_psram esp_timer log)
