/*
 * GB/GBC Emulator App (gnuboy) — OTA slot ota_1
 */
#include <stdio.h>
#include "app_common.h"
#include "gnuboy_run.h"

void app_main(void)
{
    app_init();

    char rom_path[256];
    if (app_get_rom_path(rom_path, sizeof(rom_path)) != 0) {
        printf("GB app: No ROM path found, returning to launcher\n");
        app_return_to_launcher();
    }

    printf("GB app: Running ROM: %s\n", rom_path);
    gnuboy_run(rom_path);
    app_return_to_launcher();
}
