/*
 * SMS/GG/COL Emulator App (smsplus) — OTA slot ota_2
 */
#include <stdio.h>
#include "app_common.h"
#include "smsplus_run.h"

void app_main(void)
{
    app_init();

    char rom_path[256];
    if (app_get_rom_path(rom_path, sizeof(rom_path)) != 0) {
        printf("SMS app: No ROM path found, returning to launcher\n");
        app_return_to_launcher();
    }

    printf("SMS app: Running ROM: %s\n", rom_path);
    smsplus_run(rom_path);
    app_return_to_launcher();
}
