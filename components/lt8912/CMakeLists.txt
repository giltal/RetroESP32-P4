if(CONFIG_HDMI_OUTPUT)
    idf_component_register(SRCS "lt8912.c"
                           INCLUDE_DIRS "include"
                           REQUIRES driver)
else()
    idf_component_register(INCLUDE_DIRS "include")
endif()
