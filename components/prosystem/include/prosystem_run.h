/*
 * prosystem_run.h — Entry point for Atari 7800 (prosystem) emulator on ESP32-P4
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Run the Atari 7800 emulator.
 * Loads the ROM, runs emulation, returns to launcher on exit.
 * @param rom_path Full path to the .a78 ROM file
 */
void prosystem_run(const char *rom_path);

#ifdef __cplusplus
}
#endif
