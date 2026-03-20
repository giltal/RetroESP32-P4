/*
 * PC Engine (HuExpress) app for ESP32-P4
 * Standard app_common bootstrap → huexpress_run()
 */
#include <stdio.h>
#include "app_common.h"
#include "huexpress_run.h"

void app_main(void)
{
    app_init();

    char rom_path[272];
    if (app_get_rom_path(rom_path, sizeof(rom_path)) != 0) {
        printf("PCE: No ROM path — returning to launcher\n");
        app_return_to_launcher();
        return;
    }

    huexpress_run(rom_path);

    app_return_to_launcher();
}
