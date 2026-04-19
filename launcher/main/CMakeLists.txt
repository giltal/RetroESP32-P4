# Both LCD/touch and HDMI components are always listed as requirements
# so CMake resolves dependencies before Kconfig.  The C code selects the
# active path at compile time via CONFIG_HDMI_OUTPUT.
idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "." "includes" "sprites"
    REQUIRES odroid st7701_lcd gt911_touch hdmi_display
             ppa_engine gamepad audio pngaux
             driver esp_lcd esp_psram fatfs vfs sdmmc nvs_flash
             esp_partition esp_app_format esp_event app_update
             psram_app_loader serial_upload
)

# The original RetroESP32 code uses unsafe sprintf patterns.
target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wno-error=format-overflow
    -Wno-error=restrict
    -Wno-error=format-truncation
)
