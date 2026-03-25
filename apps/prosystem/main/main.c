/*
 * Atari 7800 Emulator App (prosystem) — OTA slot ota_5
 */
#include <stdio.h>
#include "app_common.h"
#include "prosystem_run.h"

void app_main(void)
{
    app_init();

    char rom_path[256];
    if (app_get_rom_path(rom_path, sizeof(rom_path)) != 0) {
        printf("ProSystem app: No ROM path found, returning to launcher\n");
        app_return_to_launcher();
    }

    printf("ProSystem app: Running ROM: %s\n", rom_path);
    prosystem_run(rom_path);
    app_return_to_launcher();
}
