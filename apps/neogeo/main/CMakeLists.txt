idf_component_register(
    SRCS "main.c" "neogeo_run.c"
    INCLUDE_DIRS "."
    REQUIRES app_common gngeo odroid st7701_lcd esp_timer
)
