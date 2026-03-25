idf_component_register(
    SRCS "main.c" "genesis_run.c"
    INCLUDE_DIRS "."
    REQUIRES app_common gwenesis odroid st7701_lcd esp_timer
)

target_compile_options(${COMPONENT_LIB} PRIVATE -O3)
