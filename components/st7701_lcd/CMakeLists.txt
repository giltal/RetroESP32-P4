idf_component_register(
    SRCS "st7701_lcd.c" "esp_lcd_st7701.c" "esp_lcd_st7701_mipi.c"
    INCLUDE_DIRS "include"
    REQUIRES driver esp_lcd esp_hw_support ppa_engine
    PRIV_REQUIRES esp_mm
)
