#include <stdio.h>
#include "app_common.h"
#include "atari800_run.h"

void app_main(void) {
    app_init();
    char rom_path[256];
    if (app_get_rom_path(rom_path, sizeof(rom_path)) != 0) {
        printf("No ROM path found, returning to launcher\n");
        app_return_to_launcher();
    }
    atari800_run(rom_path);
    app_return_to_launcher();
}
