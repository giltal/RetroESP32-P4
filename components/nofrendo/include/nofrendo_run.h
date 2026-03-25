#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Run the NES (nofrendo) emulator
 * @param rom_path Full path to the .nes ROM file on SD card (e.g. "/sd/roms/nes/game.nes")
 * 
 * This function loads the ROM into PSRAM, runs the nofrendo NES emulator,
 * and returns when the user presses MENU to quit.
 */
void nofrendo_run(const char *rom_path);

#ifdef __cplusplus
}
#endif
