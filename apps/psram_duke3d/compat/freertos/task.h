/* Stub for PSRAM app build */
#ifndef PAPP_COMPAT_FREERTOS_TASK_H
#define PAPP_COMPAT_FREERTOS_TASK_H

#include "FreeRTOS.h"

static inline void vTaskDelay(TickType_t ticks) { (void)ticks; }
static inline void taskYIELD(void) {}

#endif
