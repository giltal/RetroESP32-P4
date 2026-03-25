/*
 * Atari Lynx Emulator App (handy) — OTA slot ota_6
 */
#include <stdio.h>
#include "app_common.h"

extern void handy_run(const char *rom_path);

void app_main(void)
{
    app_init();

    char rom_path[256];
    if (app_get_rom_path(rom_path, sizeof(rom_path)) != 0) {
        printf("Handy app: No ROM path found, returning to launcher\n");
        app_return_to_launcher();
    }

    printf("Handy app: Running ROM: %s\n", rom_path);
    handy_run(rom_path);
    app_return_to_launcher();
}
