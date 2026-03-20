/*
 * nofrendo_run.c — Entry point for the NES (nofrendo) emulator on ESP32-P4
 *
 * Loads a .nes ROM from SD card into PSRAM, runs the nofrendo emulator,
 * and returns to the launcher when the user presses MENU.
 */

#include "nofrendo_run.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_heap_caps.h"
#include "esp_system.h"
#include "esp_timer.h"

#include "odroid_settings.h"
#include "odroid_sdcard.h"
#include "odroid_display.h"
#include "odroid_input.h"
#include "odroid_system.h"
#include "nofrendo.h"

/* ROM data buffer in PSRAM — returned by osd_getromdata() */
static char *rom_data = NULL;

/* Quit flag — set by DoQuit() in video_audio.c */
extern volatile bool nofrendo_quit_flag;

/* Extern: forceConsoleReset flag used by nes_emulate */
extern bool forceConsoleReset;

/* SD card base path constant used by nesstate.c */
extern const char* SD_BASE_PATH;

/*
 * Called by nes_rom.c to get a pointer to the loaded ROM data.
 * In the original ESP32 build this returned (char*)0x3f800000.
 * We return our PSRAM-allocated buffer instead.
 */
char *osd_getromdata(void)
{
    printf("osd_getromdata: ROM@%p\n", rom_data);
    return rom_data;
}

void nofrendo_run(const char *rom_path)
{
    printf("nofrendo_run: starting NES emulator, ROM=%s\n", rom_path);

    /* Reset quit flag */
    nofrendo_quit_flag = false;

    /* Default: force a console reset (fresh start) */
    forceConsoleReset = true;

    /* Store ROM path in settings so load_sram/save_sram can find it */
    odroid_settings_RomFilePath_set(rom_path);

    /* Extract just the filename for nofrendo_main args */
    char *fileName = odroid_util_GetFileName(rom_path);
    if (!fileName)
    {
        printf("nofrendo_run: ERROR — could not extract filename from '%s'\n", rom_path);
        return;
    }

    /* Allocate 4MB in PSRAM for ROM data */
    rom_data = (char *)heap_caps_malloc(0x400000, MALLOC_CAP_SPIRAM);
    if (!rom_data)
    {
        printf("nofrendo_run: ERROR — failed to allocate 4MB PSRAM for ROM\n");
        free(fileName);
        return;
    }

    /* Open SD card and load ROM */
    esp_err_t r = odroid_sdcard_open("/sd");
    if (r != ESP_OK)
    {
        printf("nofrendo_run: ERROR — SD card open failed (%d)\n", r);
        heap_caps_free(rom_data);
        rom_data = NULL;
        free(fileName);
        return;
    }

    size_t fileSize = odroid_sdcard_copy_file_to_memory(rom_path, rom_data);
    printf("nofrendo_run: ROM loaded, size=%d bytes\n", (int)fileSize);

    if (fileSize == 0)
    {
        printf("nofrendo_run: ERROR — ROM file empty or not found\n");
        odroid_sdcard_close();
        heap_caps_free(rom_data);
        rom_data = NULL;
        free(fileName);
        return;
    }

    odroid_sdcard_close();

    /* If resuming from save state, do NOT reset — let the loaded state persist */
    if (odroid_settings_StartAction_get() == ODROID_START_ACTION_RESTART)
    {
        forceConsoleReset = false;
        odroid_settings_StartAction_set(ODROID_START_ACTION_NORMAL);
    }

    /* --- Run the emulator --- */
    printf("nofrendo_run: calling nofrendo_main\n");

    char *args[1] = { fileName };
    nofrendo_main(1, args);

    /* nofrendo_main returns when console.quit is set (triggered by DoQuit → nes_poweroff → main_quit) */
    printf("nofrendo_run: nofrendo_main returned\n");

    /* --- Cleanup --- */
    if (rom_data)
    {
        heap_caps_free(rom_data);
        rom_data = NULL;
    }

    /* NOTE: fileName was passed into nofrendo_main and stored as console.filename/nextfilename.
     * shutdown_everything() in nofrendo.c already freed it — do NOT free again here. */

    nofrendo_quit_flag = false;

    printf("nofrendo_run: returning to launcher\n");
}
