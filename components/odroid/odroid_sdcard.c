/*
 * Odroid SD Card Compatibility Layer — ESP32-P4 Implementation
 *
 * Mounts the SD card via SDMMC 4-bit mode with on-chip LDO power control.
 * Uses the same GPIO assignments as the ESP32-P4 base project.
 */

#include "odroid_sdcard.h"
#include "pins_config.h"

#include <string.h>
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "driver/sdmmc_host.h"
#include "sdmmc_cmd.h"
#include "sd_pwr_ctrl_by_on_chip_ldo.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "odroid_sdcard";
static bool s_mounted = false;

/* Global SD card base path — referenced by emulator components */
const char* SD_BASE_PATH = "/sd";

esp_err_t odroid_sdcard_open(const char* base_path)
{
    if (s_mounted) {
        ESP_LOGW(TAG, "SD card already mounted");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Initializing SD card (SDMMC mode with on-chip LDO)");

    /* Create on-chip LDO power control (channel 4, 3.3 V) */
    sd_pwr_ctrl_ldo_config_t ldo_config = {
        .ldo_chan_id = 4,
    };
    sd_pwr_ctrl_handle_t pwr_ctrl_handle = NULL;
    esp_err_t ret = sd_pwr_ctrl_new_on_chip_ldo(&ldo_config, &pwr_ctrl_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create SD power control LDO (0x%x)", ret);
        return ret;
    }

    const int max_attempts = 3;
    for (int attempt = 1; attempt <= max_attempts; attempt++) {
        ESP_LOGI(TAG, "SD mount attempt %d/%d", attempt, max_attempts);

        sdmmc_host_t host = SDMMC_HOST_DEFAULT();
        host.slot = SDMMC_HOST_SLOT_0;
        host.max_freq_khz = SDMMC_FREQ_DEFAULT;
        host.pwr_ctrl_handle = pwr_ctrl_handle;

        sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
        slot_config.width = 4;
        slot_config.clk = (gpio_num_t)SD_MMC_CLK;
        slot_config.cmd = (gpio_num_t)SD_MMC_CMD;
        slot_config.d0  = (gpio_num_t)SD_MMC_D0;
        slot_config.d1  = (gpio_num_t)SD_MMC_D1;
        slot_config.d2  = (gpio_num_t)SD_MMC_D2;
        slot_config.d3  = (gpio_num_t)SD_MMC_D3;
        slot_config.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

        esp_vfs_fat_sdmmc_mount_config_t mount_config = {
            .format_if_mount_failed = false,
            .max_files = 10,
            .allocation_unit_size = 16 * 1024
        };

        sdmmc_card_t *card = NULL;
        ret = esp_vfs_fat_sdmmc_mount(base_path, &host, &slot_config,
                                       &mount_config, &card);
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "SD Card mounted at %s (attempt %d)", base_path, attempt);
            sdmmc_card_print_info(stdout, card);
            s_mounted = true;
            return ESP_OK;
        }

        ESP_LOGW(TAG, "SD mount failed (0x%x) on attempt %d", ret, attempt);

        if (attempt < max_attempts) {
            ESP_LOGI(TAG, "Power-cycling SD card via LDO...");
            sd_pwr_ctrl_del_on_chip_ldo(pwr_ctrl_handle);
            pwr_ctrl_handle = NULL;
            vTaskDelay(pdMS_TO_TICKS(500));
            ret = sd_pwr_ctrl_new_on_chip_ldo(&ldo_config, &pwr_ctrl_handle);
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "Failed to recreate LDO (0x%x)", ret);
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(200));
        }
    }

    ESP_LOGE(TAG, "Failed to mount SD card after %d attempts", max_attempts);
    return ESP_FAIL;
}

bool odroid_sdcard_is_mounted(void)
{
    return s_mounted;
}

esp_err_t odroid_sdcard_close(void)
{
    if (!s_mounted) return ESP_OK;
    /* Note: on P4, we keep the SD card mounted persistently */
    ESP_LOGI(TAG, "odroid_sdcard_close() called (keeping mounted for P4)");
    return ESP_OK;
}

size_t odroid_sdcard_get_filesize(const char* path)
{
    if (!path) return 0;
    FILE* f = fopen(path, "rb");
    if (!f) return 0;
    fseek(f, 0, SEEK_END);
    size_t size = ftell(f);
    fclose(f);
    return size;
}

size_t odroid_sdcard_copy_file_to_memory(const char* path, void* ptr)
{
    if (!path || !ptr) return 0;

    FILE* f = fopen(path, "rb");
    if (!f) {
        ESP_LOGE(TAG, "Failed to open file: %s", path);
        return 0;
    }

    fseek(f, 0, SEEK_END);
    size_t filesize = ftell(f);
    fseek(f, 0, SEEK_SET);

    size_t total = 0;
    const size_t block = 4096;
    while (total < filesize) {
        size_t to_read = filesize - total;
        if (to_read > block) to_read = block;
        size_t read = fread((uint8_t*)ptr + total, 1, to_read, f);
        if (read == 0) break;
        total += read;
    }

    fclose(f);
    ESP_LOGI(TAG, "Copied %d bytes from %s", (int)total, path);
    return total;
}

char* odroid_sdcard_create_savefile_path(const char* base_path, const char* fileName)
{
    if (!base_path || !fileName) return NULL;

    /* Create path: <base_path>/odroid/data/<fileName> */
    size_t len = strlen(base_path) + strlen("/odroid/data/") + strlen(fileName) + 1;
    char* path = malloc(len);
    if (!path) return NULL;

    snprintf(path, len, "%s/odroid/data/%s", base_path, fileName);
    return path;
}
