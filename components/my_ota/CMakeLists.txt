idf_component_register(SRCS "my_ota.cpp"
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES app_update esp_app_format my_log log freertos)
