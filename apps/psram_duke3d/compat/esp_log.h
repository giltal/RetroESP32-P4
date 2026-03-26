/* Stub for PSRAM app build */
#ifndef PAPP_COMPAT_ESP_LOG_H
#define PAPP_COMPAT_ESP_LOG_H

#define ESP_LOGE(tag, fmt, ...)
#define ESP_LOGW(tag, fmt, ...)
#define ESP_LOGI(tag, fmt, ...)
#define ESP_LOGD(tag, fmt, ...)
#define ESP_LOGV(tag, fmt, ...)
#define ESP_LOG_LEVEL_LOCAL(level, tag, fmt, ...)

#define ESP_LOG_ERROR   1
#define ESP_LOG_WARN    2
#define ESP_LOG_INFO    3
#define ESP_LOG_DEBUG   4
#define ESP_LOG_VERBOSE 5
#define LOG_LOCAL_LEVEL ESP_LOG_ERROR

#endif
