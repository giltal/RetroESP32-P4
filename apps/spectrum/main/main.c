/*
 * ZX Spectrum Emulator App — OTA slot ota_3
 */
#include <stdio.h>
#include "app_common.h"
#include "spectrum_run.h"

void app_main(void)
{
    app_init();

    char rom_path[256];
    if (app_get_rom_path(rom_path, sizeof(rom_path)) != 0) {
        printf("Spectrum app: No ROM path found, returning to launcher\n");
        app_return_to_launcher();
    }

    printf("Spectrum app: Running ROM: %s\n", rom_path);
    spectrum_run(rom_path);
    app_return_to_launcher();
}
