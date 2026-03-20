/*
 * app_common.h — Shared initialization/exit for OTA-based emulator apps
 *
 * Provides:
 *   app_init()               - Full hardware init + NVS + SD mount
 *   app_get_rom_path()       - Read ROM path from NVS (set by launcher)
 *   app_return_to_launcher() - Switch OTA to factory partition + reboot
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialize all hardware: NVS, I2C, PPA, USB gamepad, audio, LCD, touch, SD.
 * Automatically calls app_check_safe_boot() at the end.
 * Call this first in every emulator app's app_main().
 */
void app_init(void);

/**
 * Safe-boot check: polls gamepad for ~500ms.
 * If button A is held, immediately returns to the launcher (factory partition).
 * Called automatically at the end of app_init().
 */
void app_check_safe_boot(void);

/**
 * Read the ROM file path saved by the launcher in NVS.
 * @param buf   Buffer to receive the null-terminated path string
 * @param buflen Size of buf in bytes
 * @return 0 on success, -1 if no path was set
 */
int app_get_rom_path(char *buf, int buflen);

/**
 * Switch the OTA boot partition back to the factory slot (launcher)
 * and reboot.  Does not return.
 */
void app_return_to_launcher(void) __attribute__((noreturn));

#ifdef __cplusplus
}
#endif
