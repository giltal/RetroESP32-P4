/*
 * Odroid System Compatibility Layer for ESP32-P4
 *
 * System initialization and OTA application switching stubs.
 */
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the system (I2C, PPA, LCD, Touch, Audio, Gamepad).
 * This sets up all hardware peripherals for the ESP32-P4 platform.
 */
void odroid_system_init(void);

/**
 * @brief Set the OTA application slot to boot on next restart.
 * On the P4, this is a stub that logs the requested slot.
 * Future: will implement actual emulator switching.
 *
 * @param slot OTA slot number (1-10 = emulator, 0 = launcher)
 */
void odroid_system_application_set(int slot);

/**
 * @brief Enter deep sleep (stub on P4).
 */
void odroid_system_sleep(void);

/**
 * @brief Set the indicator LED (stub on P4 — no LED).
 * @param value 0=off, 1=on
 */
void odroid_system_led_set(int value);

#ifdef __cplusplus
}
#endif
