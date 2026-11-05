idf_component_register(SRCS "otr_i2c.cpp"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_driver_i2c
                       PRIV_REQUIRES esp_timer esp_rom freertos log)
