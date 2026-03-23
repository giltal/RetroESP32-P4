/*
 * Sega Genesis / Mega Drive Emulator App (Gwenesis) — OTA slot ota_11
 *
 * Reads ROM path from NVS (set by launcher), runs the Genesis emulator,
 * then switches back to the launcher (factory partition) and reboots.
 */
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_heap_caps.h"
#include "app_common.h"
#include "genesis_run.h"

#define EMU_STACK_SIZE  (48 * 1024)   /* 48 KB — M68K + Z80 + VDP call chain */

static SemaphoreHandle_t emu_done;

static void emu_task(void *arg)
{
    genesis_run((const char *)arg);
    xSemaphoreGive(emu_done);
    vTaskDelete(NULL);
}

void app_main(void)
{
    app_init();

    char rom_path[256];
    if (app_get_rom_path(rom_path, sizeof(rom_path)) != 0) {
        printf("Genesis app: No ROM path found, returning to launcher\n");
        app_return_to_launcher();
    }

    printf("Genesis app: Running ROM: %s\n", rom_path);

    emu_done = xSemaphoreCreateBinary();

    /* Allocate the task stack in PSRAM so it doesn't eat internal RAM */
    StaticTask_t *tcb = heap_caps_calloc(1, sizeof(StaticTask_t), MALLOC_CAP_SPIRAM);
    StackType_t  *stk = heap_caps_calloc(1, EMU_STACK_SIZE, MALLOC_CAP_SPIRAM);
    if (!tcb || !stk) {
        printf("Genesis app: Failed to allocate emu task stack\n");
        app_return_to_launcher();
    }

    xTaskCreateStaticPinnedToCore(emu_task, "genesis_emu",
                                  EMU_STACK_SIZE / sizeof(StackType_t),
                                  rom_path, 5, stk, tcb, 0);

    /* Wait for emulation to finish */
    xSemaphoreTake(emu_done, portMAX_DELAY);
    vSemaphoreDelete(emu_done);
    heap_caps_free(stk);
    heap_caps_free(tcb);

    app_return_to_launcher();
}
