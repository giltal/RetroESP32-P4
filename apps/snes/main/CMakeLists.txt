idf_component_register(
    SRCS "main.c" "snes_run.c"
    INCLUDE_DIRS "."
    REQUIRES app_common snes9x odroid st7701_lcd
)
