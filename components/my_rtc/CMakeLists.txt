if(CONFIG_OTR_HW_PROFILE_P4_43)
  set(rtc_src "my_rtc.cpp" "my_rtc_internal.cpp" "my_rtc_auto.cpp")
else()
  set(rtc_src "my_rtc.cpp")
endif()

idf_component_register(SRCS ${rtc_src}
                       INCLUDE_DIRS "include"
                       REQUIRES esp_driver_i2c
                       PRIV_REQUIRES log esp_timer freertos my_log esp_hw_support esp_system)
