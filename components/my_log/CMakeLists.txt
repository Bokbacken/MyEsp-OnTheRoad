idf_component_register(SRCS "my_log.cpp"
                       INCLUDE_DIRS "include"
                       REQUIRES log esp_timer
                       PRIV_REQUIRES heap freertos)
