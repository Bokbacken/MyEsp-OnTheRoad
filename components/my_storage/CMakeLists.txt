idf_component_register(SRCS "my_storage.cpp"
                       INCLUDE_DIRS "include"
                       REQUIRES nvs_flash
                       PRIV_REQUIRES esp_partition fatfs wear_levelling vfs log my_log)
