/*
 * OpenTyrian app wrapper for ESP32-P4 RetroESP32
 *
 * Standalone game — no ROM file needed.
 * Game data must be at /sd/tyrian/data/ on the SD card.
 */
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "app_common.h"
#include "opentyr.h"
#include "odroid_input.h"
#include "odroid_display.h"

/* Background task: polls MENU button for return-to-launcher */
static void launcher_watchdog_task(void *pvParameters)
{
    odroid_gamepad_state state;
    int held_ms = 0;

    for (;;) {
        odroid_input_gamepad_read(&state);
        if (state.values[ODROID_INPUT_MENU]) {
            held_ms += 100;
            if (held_ms >= 3000) {
                printf("OpenTyrian: MENU held 3s, returning to launcher\n");
                app_return_to_launcher();
            }
        } else {
            held_ms = 0;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

static void tyrian_task(void *pvParameters)
{
    char *argv[] = {"opentyrian", NULL};
    main(1, argv);

    /* If game exits normally, return to launcher */
    printf("OpenTyrian: game exited, returning to launcher\n");
    app_return_to_launcher();
}

void app_main(void)
{
    app_init();

    /* OpenTyrian is 320×200 — after 270° rotation (240×320), scale
     * 2.0×2.5 fills the full 480×800 LCD with no black bars. */
    display_set_scale(2.0f, 2.5f);

    printf("OpenTyrian: Starting game...\n");

    /* Launcher return watchdog on core 1 */
    xTaskCreatePinnedToCore(launcher_watchdog_task, "launcherWD", 4096, NULL, 1, NULL, 1);

    /* Game task on core 0 with large stack */
    xTaskCreatePinnedToCore(tyrian_task, "tyrianTask", 34000, NULL, 5, NULL, 0);

    /* app_main returns, FreeRTOS scheduler keeps tasks running */
}
