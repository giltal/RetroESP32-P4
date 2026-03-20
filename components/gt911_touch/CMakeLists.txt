idf_component_register(
    SRCS "gt911_touch.c" "esp_lcd_touch.c" "esp_lcd_touch_gt911.c"
    INCLUDE_DIRS "include"
    REQUIRES driver esp_lcd
)
