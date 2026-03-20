/*
 * NES Emulator App (nofrendo) — OTA slot ota_0
 *
 * Reads ROM path from NVS (set by launcher), runs the NES emulator,
 * then switches back to the launcher (factory partition) and reboots.
 */
#include <stdio.h>
#include "app_common.h"
#include "nofrendo_run.h"

void app_main(void)
{
    /* Initialize hardware: NVS, display, audio, gamepad, SD */
    app_init();

    /* Read ROM path saved by launcher */
    char rom_path[256];
    if (app_get_rom_path(rom_path, sizeof(rom_path)) != 0) {
        printf("NES app: No ROM path found, returning to launcher\n");
        app_return_to_launcher();
    }

    printf("NES app: Running ROM: %s\n", rom_path);

    /* Run the NES emulator — blocks until user exits */
    nofrendo_run(rom_path);

    /* Return to launcher */
    app_return_to_launcher();
}
