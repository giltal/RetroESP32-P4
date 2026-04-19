# Both st7701_lcd/gt911_touch and hdmi_display are always listed so the
# component dependency graph is resolved before Kconfig runs.  The actual
# code paths are selected at compile-time via CONFIG_HDMI_OUTPUT.
set(ODROID_REQUIRES ppa_engine gamepad audio driver esp_lcd esp_psram fatfs vfs sdmmc nvs_flash esp_partition esp_app_format esp_adc
                    st7701_lcd gt911_touch hdmi_display esp_hw_support esp_mm)

idf_component_register(
    SRCS "odroid_display.c" "odroid_input.c" "odroid_settings.c" "odroid_sdcard.c" "odroid_system.c" "odroid_audio.c"
    INCLUDE_DIRS "include"
    REQUIRES ${ODROID_REQUIRES}
    PRIV_REQUIRES esp_timer
)
