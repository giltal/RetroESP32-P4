#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Run the SMS/Game Gear (smsplus) emulator
 * @param rom_path Full path to the .sms or .gg ROM file on SD card
 * 
 * This function loads the ROM, initialises the SMS/GG emulator,
 * runs the main emulation loop, and returns when MENU is pressed.
 */
void smsplus_run(const char *rom_path);

#ifdef __cplusplus
}
#endif
