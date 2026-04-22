/*
 * Neo Geo Emulator — App Entry Point
 * Follows the same pattern as genesis/main/main.c
 */
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "app_common.h"

#define EMU_STACK_SIZE  (48 * 1024)

static const char *TAG = "neogeo";
static SemaphoreHandle_t emu_done;

extern void neogeo_run(const char *rom_path);

static void emu_task(void *arg)
{
    const char *rom_path = (const char *)arg;
    ESP_LOGI(TAG, "Starting Neo Geo emulation: %s", rom_path);
    neogeo_run(rom_path);
    ESP_LOGI(TAG, "Emulation ended");
    xSemaphoreGive(emu_done);
    vTaskDelete(NULL);
}

void app_main(void)
{
    app_init();

    static char rom_path[512];
    app_get_rom_path(rom_path, sizeof(rom_path));

    if (rom_path[0] == '\0') {
        ESP_LOGE(TAG, "No ROM path set");
        app_return_to_launcher();
        return;
    }

    ESP_LOGI(TAG, "ROM: %s", rom_path);

    emu_done = xSemaphoreCreateBinary();

    /* Allocate task stack in PSRAM (internal RAM too small for 48KB) */
    StaticTask_t *tcb = heap_caps_calloc(1, sizeof(StaticTask_t), MALLOC_CAP_SPIRAM);
    StackType_t  *stk = heap_caps_calloc(1, EMU_STACK_SIZE, MALLOC_CAP_SPIRAM);
    if (!tcb || !stk) {
        ESP_LOGE(TAG, "Failed to allocate emu task stack in PSRAM");
        app_return_to_launcher();
        return;
    }

    xTaskCreateStaticPinnedToCore(emu_task, "neogeo_emu",
                                  EMU_STACK_SIZE / sizeof(StackType_t),
                                  rom_path, 5, stk, tcb, 0);

    xSemaphoreTake(emu_done, portMAX_DELAY);
    vSemaphoreDelete(emu_done);
    heap_caps_free(stk);
    heap_caps_free(tcb);

    app_return_to_launcher();
}
