/*
 * Odroid Settings Compatibility Layer — ESP32-P4 Implementation
 *
 * Stores settings in NVS using the same key names as the original
 * RetroESP32 firmware for Odroid Go.
 */

#include "odroid_settings.h"

#include <string.h>
#include <stdlib.h>
#include "nvs_flash.h"
#include "esp_log.h"

static const char *TAG = "odroid_settings";
static const char *NVS_NAMESPACE = "Odroid";

/* ─── Helpers ─────────────────────────────────────────────────── */

static int32_t nvs_get_i32_default(const char *key, int32_t def)
{
    nvs_handle_t handle;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle) != ESP_OK) return def;
    int32_t val = def;
    nvs_get_i32(handle, key, &val);
    nvs_close(handle);
    return val;
}

static void nvs_set_i32_commit(const char *key, int32_t val)
{
    nvs_handle_t handle;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle) != ESP_OK) return;
    nvs_set_i32(handle, key, val);
    nvs_commit(handle);
    nvs_close(handle);
}

/* ─── Utility Functions ───────────────────────────────────────── */

char* odroid_util_GetFileName(const char* path)
{
    if (!path) return NULL;
    const char* last_sep = strrchr(path, '/');
    if (!last_sep) last_sep = strrchr(path, '\\');
    const char* name = last_sep ? last_sep + 1 : path;
    return strdup(name);
}

char* odroid_util_GetFileExtenstion(const char* path)
{
    if (!path) return NULL;
    const char* dot = strrchr(path, '.');
    if (!dot) return strdup("");
    return strdup(dot);
}

char* odroid_util_GetFileNameWithoutExtension(const char* path)
{
    char* filename = odroid_util_GetFileName(path);
    if (!filename) return NULL;
    char* dot = strrchr(filename, '.');
    if (dot) *dot = '\0';
    return filename;
}

/* ─── Volume ──────────────────────────────────────────────────── */

int32_t odroid_settings_Volume_get(void)
{
    return nvs_get_i32_default("Volume", 4);
}

void odroid_settings_Volume_set(int32_t value)
{
    nvs_set_i32_commit("Volume", value);
}

/* ─── Backlight / Brightness ──────────────────────────────────── */

int32_t odroid_settings_Backlight_get(void)
{
    return nvs_get_i32_default("Backlight", 5);
}

void odroid_settings_Backlight_set(int32_t value)
{
    nvs_set_i32_commit("Backlight", value);
}

/* ─── ROM File Path ───────────────────────────────────────────── */

static char s_rom_path[256] = "";

char* odroid_settings_RomFilePath_get(void)
{
    nvs_handle_t handle;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle) != ESP_OK) return strdup(s_rom_path);
    size_t len = sizeof(s_rom_path);
    nvs_get_str(handle, "RomPath", s_rom_path, &len);
    nvs_close(handle);
    return strdup(s_rom_path);
}

void odroid_settings_RomFilePath_set(const char* path)
{
    if (!path) return;
    strncpy(s_rom_path, path, sizeof(s_rom_path) - 1);
    s_rom_path[sizeof(s_rom_path) - 1] = '\0';

    nvs_handle_t handle;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle) != ESP_OK) return;
    nvs_set_str(handle, "RomPath", s_rom_path);
    nvs_commit(handle);
    nvs_close(handle);
}

/* ─── DataSlot ────────────────────────────────────────────────── */

int32_t odroid_settings_DataSlot_get(void)
{
    return nvs_get_i32_default("DataSlot", 0);
}

void odroid_settings_DataSlot_set(int32_t value)
{
    nvs_set_i32_commit("DataSlot", value);
}

/* ─── AppSlot ─────────────────────────────────────────────────── */

int32_t odroid_settings_AppSlot_get(void)
{
    return nvs_get_i32_default("AppSlot", 0);
}

void odroid_settings_AppSlot_set(int32_t value)
{
    nvs_set_i32_commit("AppSlot", value);
}

/* ─── VRef ────────────────────────────────────────────────────── */

int32_t odroid_settings_VRef_get(void)
{
    return nvs_get_i32_default("VRef", 1100);
}

void odroid_settings_VRef_set(int32_t value)
{
    nvs_set_i32_commit("VRef", value);
}

/* ─── Start Action ────────────────────────────────────────────── */

ODROID_START_ACTION odroid_settings_StartAction_get(void)
{
    return (ODROID_START_ACTION)nvs_get_i32_default("StartAction", 0);
}

void odroid_settings_StartAction_set(ODROID_START_ACTION value)
{
    nvs_set_i32_commit("StartAction", (int32_t)value);
}

/* ─── Audio Sink ──────────────────────────────────────────────── */

ODROID_AUDIO_SINK odroid_settings_AudioSink_get(void)
{
    return (ODROID_AUDIO_SINK)nvs_get_i32_default("AudioSink", 0);
}

void odroid_settings_AudioSink_set(ODROID_AUDIO_SINK value)
{
    nvs_set_i32_commit("AudioSink", (int32_t)value);
}

/* ─── Scale Disabled (per-system) ─────────────────────────────── */

uint8_t odroid_settings_ScaleDisabled_get(ODROID_SCALE_DISABLE system)
{
    return (uint8_t)nvs_get_i32_default("ScaleOff", 0) & (uint8_t)system;
}

void odroid_settings_ScaleDisabled_set(ODROID_SCALE_DISABLE system, uint8_t value)
{
    int32_t current = nvs_get_i32_default("ScaleOff", 0);
    if (value)
        current |= (int32_t)system;
    else
        current &= ~(int32_t)system;
    nvs_set_i32_commit("ScaleOff", current);
}
