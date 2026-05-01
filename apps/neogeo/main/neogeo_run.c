/*
 * Neo Geo Emulator — Run Loop
 * Phase 2: ROM loading and BIOS boot
 */
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "odroid_display.h"
#include "odroid_audio.h"
#include "odroid_input.h"
#include "odroid_sdcard.h"
#include "odroid_system.h"

/* GnGeo headers */
#include "emu.h"
#include "memory.h"
#include "roms.h"
#include "video.h"
#include "screen.h"
#include "conf.h"

static const char *TAG = "neogeo_run";

/* Defined in esp32_platform.c */
extern void esp32_set_rompath(const char *path);
extern void esp32_enable_sound(int enable);
extern void esp32_init_conf(const char *game_name);
extern void gn_loading_info(const char *msg);
extern void gn_set_loading_game(const char *name);

void neogeo_run(const char *rom_path)
{
    ESP_LOGI(TAG, "Neo Geo emulator starting...");
    ESP_LOGI(TAG, "ROM path: %s", rom_path ? rom_path : "(null)");

    /* ── Platform init ── */
    odroid_sdcard_open("/sd");
    ili9341_init();
    ili9341_clear(0x0000);

    /* Neo Geo uses X/Y as C/D buttons — disable X→Menu/Y→Volume aliasing */
    extern bool odroid_input_xy_menu_disable;
    odroid_input_xy_menu_disable = true;

    ESP_LOGI(TAG, "Free PSRAM: %u KB",
             (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024));
    ESP_LOGI(TAG, "Free internal: %u KB",
             (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024));

    /* ── Extract game name from path ── */
    /* rom_path is like "/sd/roms/neogeo/mslug.zip" — we need "mslug" */
    char game_name[64] = {0};
    const char *rom_dir = "/sd/roms/neogeo";

    if (rom_path) {
        const char *slash = strrchr(rom_path, '/');
        const char *base = slash ? slash + 1 : rom_path;
        strncpy(game_name, base, sizeof(game_name) - 1);
        /* Remove .zip extension if present */
        char *dot = strrchr(game_name, '.');
        if (dot) *dot = '\0';

        /* Extract directory from rom_path */
        if (slash) {
            int dirlen = slash - rom_path;
            if (dirlen > 0 && dirlen < 256) {
                static char dir_buf[256];
                strncpy(dir_buf, rom_path, dirlen);
                dir_buf[dirlen] = '\0';
                rom_dir = dir_buf;
            }
        }
    }

    if (game_name[0] == '\0') {
        ESP_LOGE(TAG, "No game name specified");
        goto cleanup;
    }

    ESP_LOGI(TAG, "Game: %s, ROM dir: %s", game_name, rom_dir);

    /* ── Initialize GnGeo conf ── */
    esp32_init_conf(game_name);
    esp32_set_rompath(rom_dir);
    esp32_enable_sound(1);

    /* ── Init screen (allocates framebuffers) ── */
    if (screen_init() != 0) {
        ESP_LOGE(TAG, "screen_init failed");
        goto cleanup;
    }

    ESP_LOGI(TAG, "After screen_init — Free PSRAM: %u KB",
             (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024));

    /* ── Load game ROMs and initialize emulator ── */
    ESP_LOGI(TAG, "Loading game: %s", game_name);
    gn_set_loading_game(game_name);
    gn_loading_info("Initializing...");

    if (init_game(game_name) != 1) { /* GN_TRUE = 1 */
        ESP_LOGE(TAG, "init_game failed for %s", game_name);
        goto cleanup;
    }

    ESP_LOGI(TAG, "Game loaded successfully!");
    ESP_LOGI(TAG, "After init_game — Free PSRAM: %u KB",
             (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024));
    ESP_LOGI(TAG, "P ROM: %u KB", (unsigned)(memory.rom.cpu_m68k.size / 1024));
    ESP_LOGI(TAG, "S ROM: %u KB", (unsigned)(memory.rom.game_sfix.size / 1024));
    ESP_LOGI(TAG, "Tiles: %u KB", (unsigned)(memory.rom.tiles.size / 1024));
    ESP_LOGI(TAG, "ADPCM-A: %u KB", (unsigned)(memory.rom.adpcma.size / 1024));
    ESP_LOGI(TAG, "M ROM (Z80): %u KB", (unsigned)(memory.rom.cpu_z80.size / 1024));
    ESP_LOGI(TAG, "Nb tiles: %u", (unsigned)memory.nb_of_tiles);

    /* ── Run main emulation loop ── */
    ESP_LOGI(TAG, "Starting main loop...");
    neogeo_main_loop();

    ESP_LOGI(TAG, "Emulation ended");

cleanup:
    screen_close();
    odroid_audio_terminate();
    odroid_sdcard_close();
}
