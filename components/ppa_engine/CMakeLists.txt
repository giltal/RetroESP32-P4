idf_component_register(
    SRCS "ppa_engine.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_driver_ppa esp_psram
    PRIV_REQUIRES esp_mm
)
