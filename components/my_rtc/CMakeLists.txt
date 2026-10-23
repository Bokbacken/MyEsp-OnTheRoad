idf_component_register(SRCS "my_rtc.cpp"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_driver_i2c
                       PRIV_REQUIRES log esp_timer freertos my_log)
