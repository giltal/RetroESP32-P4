idf_component_register(
    SRCS "app_common.c"
    INCLUDE_DIRS "include"
    REQUIRES odroid gt911_touch nvs_flash esp_partition app_update esp_system
)
