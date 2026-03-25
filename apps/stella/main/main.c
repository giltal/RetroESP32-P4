/*
 * Atari 2600 Emulator App (stella) — OTA slot ota_4
 */
#include <stdio.h>
#include "app_common.h"
#include "stella_run.h"

void app_main(void)
{
    app_init();

    char rom_path[256];
    if (app_get_rom_path(rom_path, sizeof(rom_path)) != 0) {
        printf("Stella app: No ROM path found, returning to launcher\n");
        app_return_to_launcher();
    }

    printf("Stella app: Running ROM: %s\n", rom_path);
    stella_run(rom_path);
    app_return_to_launcher();
}
