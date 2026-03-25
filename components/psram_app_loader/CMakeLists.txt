idf_component_register(
    SRCS "psram_app_loader.c"
    INCLUDE_DIRS "include"
    REQUIRES odroid audio gamepad esp_mm esp_psram esp_timer
             heap freertos fatfs nvs_flash
)
