/* Minimal FreeRTOS type stubs for PSRAM app builds */
#ifndef PAPP_COMPAT_FREERTOS_H
#define PAPP_COMPAT_FREERTOS_H

#include <stdint.h>

typedef void *SemaphoreHandle_t;
typedef void *TaskHandle_t;
typedef void *QueueHandle_t;
typedef void *EventGroupHandle_t;
typedef uint32_t TickType_t;
typedef uint32_t BaseType_t;
typedef unsigned int UBaseType_t;

#define portTICK_PERIOD_MS  1
#define portMAX_DELAY       0xFFFFFFFF
#define pdTRUE              1
#define pdFALSE             0
#define pdPASS              pdTRUE
#define pdMS_TO_TICKS(x)    ((TickType_t)(x))

#endif
