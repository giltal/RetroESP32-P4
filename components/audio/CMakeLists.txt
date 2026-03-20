idf_component_register(
    SRCS "audio.c"
    INCLUDE_DIRS "include"
    REQUIRES driver esp_driver_i2s esp_driver_i2c esp_driver_gpio
)
