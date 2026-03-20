/*
 * Odroid Settings Compatibility Layer for ESP32-P4
 *
 * NVS-backed settings using the same key names as the original firmware.
 */
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ─── Utility functions ───────────────────────────────────────── */
char* odroid_util_GetFileName(const char* path);
char* odroid_util_GetFileExtenstion(const char* path);
char* odroid_util_GetFileNameWithoutExtension(const char* path);

/* ─── Enumerations ────────────────────────────────────────────── */
typedef enum {
    ODROID_START_ACTION_NORMAL = 0,
    ODROID_START_ACTION_RESTART
} ODROID_START_ACTION;

typedef enum {
    ODROID_SCALE_DISABLE_NES_E = (1 << 0),
    ODROID_SCALE_DISABLE_GB_E  = (1 << 1),
    ODROID_SCALE_DISABLE_SMS_E = (1 << 2)
} ODROID_SCALE_DISABLE;

typedef enum {
    ODROID_AUDIO_SINK_SPEAKER = 0,
    ODROID_AUDIO_SINK_DAC
} ODROID_AUDIO_SINK;

/* ─── Settings ────────────────────────────────────────────────── */

/* Volume (0-4) */
int32_t odroid_settings_Volume_get(void);
void    odroid_settings_Volume_set(int32_t value);

/* Backlight/Brightness index (0-9) */
int32_t odroid_settings_Backlight_get(void);
void    odroid_settings_Backlight_set(int32_t value);

/* ROM file path (last launched ROM) */
char*   odroid_settings_RomFilePath_get(void);
void    odroid_settings_RomFilePath_set(const char* path);

/* DataSlot (0=fresh start, 1=resume from save) */
int32_t odroid_settings_DataSlot_get(void);
void    odroid_settings_DataSlot_set(int32_t value);

/* AppSlot (last emulator slot) */
int32_t odroid_settings_AppSlot_get(void);
void    odroid_settings_AppSlot_set(int32_t value);

/* VRef (ADC reference voltage) */
int32_t odroid_settings_VRef_get(void);
void    odroid_settings_VRef_set(int32_t value);

/* Start action (normal / restart-from-save) */
ODROID_START_ACTION odroid_settings_StartAction_get(void);
void odroid_settings_StartAction_set(ODROID_START_ACTION value);

/* Audio sink */
ODROID_AUDIO_SINK odroid_settings_AudioSink_get(void);
void odroid_settings_AudioSink_set(ODROID_AUDIO_SINK value);

/* Per-system scale disable */
uint8_t odroid_settings_ScaleDisabled_get(ODROID_SCALE_DISABLE system);
void    odroid_settings_ScaleDisabled_set(ODROID_SCALE_DISABLE system, uint8_t value);

#ifdef __cplusplus
}
#endif
