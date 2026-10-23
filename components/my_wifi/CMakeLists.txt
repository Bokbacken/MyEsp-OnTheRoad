idf_component_register(SRCS "my_wifi.cpp"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_wifi esp_netif esp_event
                       PRIV_REQUIRES espressif__mdns my_log heap log freertos)
