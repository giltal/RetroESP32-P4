if(CONFIG_HDMI_OUTPUT)
    idf_component_register(SRCS "hdmi_display.c"
                           INCLUDE_DIRS "include"
                           REQUIRES esp_lcd driver esp_hw_support esp_mm lt8912)
else()
    idf_component_register(INCLUDE_DIRS "include")
endif()
