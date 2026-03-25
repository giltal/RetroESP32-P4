/*
 * gnuboy_run.h — Public entry point for the Game Boy emulator
 *
 * Call gnuboy_run() from the launcher to run a Game Boy ROM.
 * The function returns when the user presses MENU to exit.
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Run the gnuboy Game Boy emulator.
 *
 * @param rom_path  Full path to the .gb/.gbc ROM file on SD card
 *                  (e.g., "/sd/roms/gb/game.gb")
 */
void gnuboy_run(const char *rom_path);

#ifdef __cplusplus
}
#endif
