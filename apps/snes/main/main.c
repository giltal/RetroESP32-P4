/*
 * SNES Emulator App (snes9x) — OTA slot ota_10
 *
 * Reads ROM path from NVS (set by launcher), runs the SNES emulator,
 * then switches back to the launcher (factory partition) and reboots.
 *
 * The emulation loop runs on a dedicated task with a 64 KB PSRAM-backed
 * stack so that DKC's deep tile-rendering call chain does not overflow
 * the 32 KB internal-RAM main-task stack.
 */
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_heap_caps.h"
#include "app_common.h"
#include "snes_run.h"

#define EMU_STACK_SIZE  (64 * 1024)   /* 64 KB — enough for DKC colour-math */

static SemaphoreHandle_t emu_done;

static void emu_task(void *arg)
{
    snes_run((const char *)arg);
    xSemaphoreGive(emu_done);
    vTaskDelete(NULL);
}

void app_main(void)
{
    app_init();

    char rom_path[256];
    if (app_get_rom_path(rom_path, sizeof(rom_path)) != 0) {
        printf("SNES app: No ROM path found, returning to launcher\n");
        app_return_to_launcher();
    }

    printf("SNES app: Running ROM: %s\n", rom_path);

    emu_done = xSemaphoreCreateBinary();

    /* Allocate the task stack in PSRAM so it doesn't eat internal RAM */
    StaticTask_t *tcb = heap_caps_calloc(1, sizeof(StaticTask_t), MALLOC_CAP_SPIRAM);
    StackType_t  *stk = heap_caps_calloc(1, EMU_STACK_SIZE, MALLOC_CAP_SPIRAM);
    if (!tcb || !stk) {
        printf("SNES app: Failed to allocate emu task stack\n");
        app_return_to_launcher();
    }

    xTaskCreateStaticPinnedToCore(emu_task, "snes_emu",
                                  EMU_STACK_SIZE / sizeof(StackType_t),
                                  rom_path, 5, stk, tcb, 0);

    /* Wait for emulation to finish (user quit / crash-guard) */
    xSemaphoreTake(emu_done, portMAX_DELAY);
    vSemaphoreDelete(emu_done);
    heap_caps_free(stk);
    heap_caps_free(tcb);

    app_return_to_launcher();
}
