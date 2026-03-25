/*
 * Odroid SD Card Compatibility Layer for ESP32-P4
 *
 * Mounts the SD card via SDMMC with on-chip LDO power control.
 * Mount point is configurable but defaults to "/sd" for compatibility.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Mount the SD card at the given base path.
 * Uses SDMMC 4-bit mode with on-chip LDO (channel 4) for IO voltage.
 * Retries up to 3 times with LDO power-cycle on failure.
 *
 * @param base_path Mount point (e.g. "/sd")
 * @return ESP_OK on success
 */
esp_err_t odroid_sdcard_open(const char* base_path);

/**
 * @brief Unmount the SD card.
 * @return ESP_OK on success
 */
esp_err_t odroid_sdcard_close(void);

/**
 * @brief Check if the SD card is mounted.
 * @return true if mounted
 */
bool odroid_sdcard_is_mounted(void);

/**
 * @brief Get the size of a file on the SD card.
 * @param path Full path to the file
 * @return File size in bytes, or 0 on error
 */
size_t odroid_sdcard_get_filesize(const char* path);

/**
 * @brief Copy a file from SD card into memory.
 * @param path Full path to the file
 * @param ptr  Destination buffer (must be large enough)
 * @return Number of bytes copied, or 0 on error
 */
size_t odroid_sdcard_copy_file_to_memory(const char* path, void* ptr);

/**
 * @brief Create a save file path from base path and filename.
 * Returns a malloc'd string like "/sd/odroid/data/<fileName>"
 * @param base_path SD base path (e.g., "/sd")
 * @param fileName  The save file name
 * @return Heap-allocated path string (caller must free)
 */
char* odroid_sdcard_create_savefile_path(const char* base_path, const char* fileName);

#ifdef __cplusplus
}
#endif
