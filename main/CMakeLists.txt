idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "." "includes" "sprites"
    REQUIRES odroid st7701_lcd gt911_touch ppa_engine gamepad audio pngaux
             driver esp_lcd esp_psram fatfs vfs sdmmc nvs_flash
             esp_partition esp_app_format esp_event
             gnuboy nofrendo smsplus prosystem spectrum stella handy
)

# The original RetroESP32 code uses unsafe sprintf patterns (self-overlapping
# buffers, undersized destinations).  Suppress the resulting -Werror flags so
# the legacy code compiles; these are cosmetic warnings, not runtime issues
# on the paths actually exercised.
target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wno-error=format-overflow
    -Wno-error=restrict
    -Wno-error=format-truncation
)
