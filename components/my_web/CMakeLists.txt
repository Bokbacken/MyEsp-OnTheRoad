idf_component_register(SRCS "my_web.cpp"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_http_server espressif__cjson
                       PRIV_REQUIRES my_log my_ota heap esp_system freertos log)
