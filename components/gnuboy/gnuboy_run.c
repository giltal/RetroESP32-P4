/*
 * gnuboy_run.c — Game Boy emulator entry point for ESP32-P4
 *
 * Based on RetroESP32 gnuboy-go/main/main.c.
 * Adapted to run as a callable function (not app_main) so the
 * launcher can call gnuboy_run() and get control back on MENU press.
 */

#include "gnuboy_run.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_heap_caps.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_log.h"

#include <string.h>
#include <strings.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>

/* Gnuboy core headers */
#include "loader.h"
#include "hw.h"
#include "lcd.h"
#include "fb.h"
#include "cpu.h"
#include "mem.h"
#include "sound.h"
#include "pcm.h"
#include "regs.h"
#include "rtc.h"
#include "gnuboy.h"
#include "input.h"

/* Odroid compatibility layer */
#include "odroid_settings.h"
#include "odroid_input.h"
#include "odroid_display.h"
#include "odroid_audio.h"
#include "odroid_system.h"
#include "odroid_sdcard.h"

static const char *TAG = "gnuboy_run";

#define GAMEBOY_WIDTH  (160)
#define GAMEBOY_HEIGHT (144)
#define AUDIO_SAMPLE_RATE (32000)

/* ─── 5×7 Bitmap Font (same as NES menu) ────────────────────────── */
static const uint8_t gb_font5x7[][7] = {
    /* ' ' */ {0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    /* 'A' */ {0x0E,0x11,0x11,0x1F,0x11,0x11,0x11},
    /* 'B' */ {0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E},
    /* 'C' */ {0x0E,0x11,0x10,0x10,0x10,0x11,0x0E},
    /* 'D' */ {0x1E,0x11,0x11,0x11,0x11,0x11,0x1E},
    /* 'E' */ {0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F},
    /* 'F' */ {0x1F,0x10,0x10,0x1E,0x10,0x10,0x10},
    /* 'G' */ {0x0E,0x11,0x10,0x17,0x11,0x11,0x0E},
    /* 'H' */ {0x11,0x11,0x11,0x1F,0x11,0x11,0x11},
    /* 'I' */ {0x0E,0x04,0x04,0x04,0x04,0x04,0x0E},
    /* 'J' */ {0x07,0x02,0x02,0x02,0x02,0x12,0x0C},
    /* 'K' */ {0x11,0x12,0x14,0x18,0x14,0x12,0x11},
    /* 'L' */ {0x10,0x10,0x10,0x10,0x10,0x10,0x1F},
    /* 'M' */ {0x11,0x1B,0x15,0x15,0x11,0x11,0x11},
    /* 'N' */ {0x11,0x19,0x15,0x13,0x11,0x11,0x11},
    /* 'O' */ {0x0E,0x11,0x11,0x11,0x11,0x11,0x0E},
    /* 'P' */ {0x1E,0x11,0x11,0x1E,0x10,0x10,0x10},
    /* 'Q' */ {0x0E,0x11,0x11,0x11,0x15,0x12,0x0D},
    /* 'R' */ {0x1E,0x11,0x11,0x1E,0x14,0x12,0x11},
    /* 'S' */ {0x0E,0x11,0x10,0x0E,0x01,0x11,0x0E},
    /* 'T' */ {0x1F,0x04,0x04,0x04,0x04,0x04,0x04},
    /* 'U' */ {0x11,0x11,0x11,0x11,0x11,0x11,0x0E},
    /* 'V' */ {0x11,0x11,0x11,0x11,0x11,0x0A,0x04},
    /* 'W' */ {0x11,0x11,0x11,0x15,0x15,0x1B,0x11},
    /* 'X' */ {0x11,0x11,0x0A,0x04,0x0A,0x11,0x11},
    /* 'Y' */ {0x11,0x11,0x0A,0x04,0x04,0x04,0x04},
    /* 'Z' */ {0x1F,0x01,0x02,0x04,0x08,0x10,0x1F},
    /* '!' */ {0x04,0x04,0x04,0x04,0x04,0x00,0x04},
    /* '>' */ {0x10,0x08,0x04,0x02,0x04,0x08,0x10},
    /* '.' */ {0x00,0x00,0x00,0x00,0x00,0x00,0x04},
    /* 'a'-'z' lowercase */
    {0x00,0x00,0x0E,0x01,0x0F,0x11,0x0F},
    {0x10,0x10,0x1E,0x11,0x11,0x11,0x1E},
    {0x00,0x00,0x0E,0x11,0x10,0x11,0x0E},
    {0x01,0x01,0x0F,0x11,0x11,0x11,0x0F},
    {0x00,0x00,0x0E,0x11,0x1F,0x10,0x0E},
    {0x06,0x08,0x08,0x1C,0x08,0x08,0x08},
    {0x00,0x00,0x0F,0x11,0x0F,0x01,0x0E},
    {0x10,0x10,0x1E,0x11,0x11,0x11,0x11},
    {0x04,0x00,0x0C,0x04,0x04,0x04,0x0E},
    {0x02,0x00,0x06,0x02,0x02,0x12,0x0C},
    {0x10,0x10,0x12,0x14,0x18,0x14,0x12},
    {0x0C,0x04,0x04,0x04,0x04,0x04,0x0E},
    {0x00,0x00,0x1A,0x15,0x15,0x15,0x15},
    {0x00,0x00,0x1E,0x11,0x11,0x11,0x11},
    {0x00,0x00,0x0E,0x11,0x11,0x11,0x0E},
    {0x00,0x00,0x1E,0x11,0x1E,0x10,0x10},
    {0x00,0x00,0x0F,0x11,0x0F,0x01,0x01},
    {0x00,0x00,0x16,0x19,0x10,0x10,0x10},
    {0x00,0x00,0x0F,0x10,0x0E,0x01,0x1E},
    {0x08,0x08,0x1C,0x08,0x08,0x09,0x06},
    {0x00,0x00,0x11,0x11,0x11,0x11,0x0F},
    {0x00,0x00,0x11,0x11,0x11,0x0A,0x04},
    {0x00,0x00,0x11,0x11,0x15,0x15,0x0A},
    {0x00,0x00,0x11,0x0A,0x04,0x0A,0x11},
    {0x00,0x00,0x11,0x11,0x0F,0x01,0x0E},
    {0x00,0x00,0x1F,0x02,0x04,0x08,0x1F},
};

static int gb_font_index(char c)
{
    if (c == ' ')  return 0;
    if (c >= 'A' && c <= 'Z') return 1 + (c - 'A');
    if (c == '!')  return 27;
    if (c == '>')  return 28;
    if (c == '.')  return 29;
    if (c >= 'a' && c <= 'z') return 30 + (c - 'a');
    if (c >= '0' && c <= '9') return 0; /* digits not in font — show space */
    return 0;
}

/* RGB565 byte-swapped colors (little-endian for our framebuffer) */
#define GB_COLOR_BLACK     0x0000
#define GB_COLOR_WHITE     0xFFFF
#define GB_COLOR_YELLOW    0xE0FF
#define GB_COLOR_DKGRAY    0x2108
#define GB_COLOR_GREEN     0xE007
#define GB_COLOR_CYAN      0xFF07

static void gb_draw_char(uint16_t *fb, int px, int py, char c, uint16_t color)
{
    int idx = gb_font_index(c);
    const uint8_t *glyph = gb_font5x7[idx];
    for (int row = 0; row < 7; row++) {
        int yy = py + row;
        if (yy < 0 || yy >= 240) continue;
        uint8_t bits = glyph[row];
        for (int col = 0; col < 5; col++) {
            int xx = px + col;
            if (xx < 0 || xx >= 320) continue;
            if (bits & (0x10 >> col)) {
                fb[yy * 320 + xx] = color;
            }
        }
    }
}

static void gb_draw_string(uint16_t *fb, int px, int py, const char *str, uint16_t color)
{
    while (*str) {
        gb_draw_char(fb, px, py, *str, color);
        px += 6;
        str++;
    }
}

static void gb_fill_rect(uint16_t *fb, int x, int y, int w, int h, uint16_t color)
{
    for (int row = y; row < y + h && row < 240; row++) {
        if (row < 0) continue;
        for (int col = x; col < x + w && col < 320; col++) {
            if (col < 0) continue;
            fb[row * 320 + col] = color;
        }
    }
}

/* ─── Volume Overlay for GB/GBC ────────────────────────────────── */
static void gb_show_volume_overlay(void)
{
    uint16_t *fb = display_get_framebuffer();
    if (!fb) return;

    static const char *level_names[ODROID_VOLUME_LEVEL_COUNT] = {
        "MUTE", "25%", "50%", "75%", "100%"
    };

    int level = (int)odroid_audio_volume_get();
    int timeout = 25;

    odroid_gamepad_state prev;
    odroid_input_gamepad_read(&prev);

    /* Debounce: wait for volume button release */
    for (int i = 0; i < 100; i++) {
        odroid_gamepad_state tmp;
        odroid_input_gamepad_read(&tmp);
        if (!tmp.values[ODROID_INPUT_VOLUME]) break;
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    odroid_input_gamepad_read(&prev);

    while (timeout > 0) {
        odroid_display_lock_gb_display();

        int box_w = 140, box_h = 34;
        int box_x = (320 - box_w) / 2, box_y = 8;

        gb_fill_rect(fb, box_x, box_y, box_w, box_h, GB_COLOR_BLACK);
        gb_fill_rect(fb, box_x, box_y, box_w, 1, GB_COLOR_WHITE);
        gb_fill_rect(fb, box_x, box_y + box_h - 1, box_w, 1, GB_COLOR_WHITE);
        gb_fill_rect(fb, box_x, box_y, 1, box_h, GB_COLOR_WHITE);
        gb_fill_rect(fb, box_x + box_w - 1, box_y, 1, box_h, GB_COLOR_WHITE);

        char title[32];
        snprintf(title, sizeof(title), "VOLUME: %s", level_names[level]);
        int title_w = strlen(title) * 6;
        gb_draw_string(fb, box_x + (box_w - title_w) / 2, box_y + 4, title, GB_COLOR_YELLOW);

        int bar_x = box_x + 10, bar_y = box_y + 16, bar_w = box_w - 20, bar_h = 10;
        gb_fill_rect(fb, bar_x, bar_y, bar_w, bar_h, GB_COLOR_DKGRAY);
        if (level > 0) {
            int fill_w = (bar_w * level) / (ODROID_VOLUME_LEVEL_COUNT - 1);
            uint16_t bar_color = (level <= 1) ? GB_COLOR_GREEN :
                                 (level <= 3) ? GB_COLOR_CYAN : GB_COLOR_YELLOW;
            gb_fill_rect(fb, bar_x, bar_y, fill_w, bar_h, bar_color);
        }
        gb_fill_rect(fb, bar_x, bar_y, bar_w, 1, GB_COLOR_WHITE);
        gb_fill_rect(fb, bar_x, bar_y + bar_h - 1, bar_w, 1, GB_COLOR_WHITE);
        gb_fill_rect(fb, bar_x, bar_y, 1, bar_h, GB_COLOR_WHITE);
        gb_fill_rect(fb, bar_x + bar_w - 1, bar_y, 1, bar_h, GB_COLOR_WHITE);
        for (int i = 1; i < ODROID_VOLUME_LEVEL_COUNT - 1; i++) {
            int sx = bar_x + (bar_w * i) / (ODROID_VOLUME_LEVEL_COUNT - 1);
            gb_fill_rect(fb, sx, bar_y, 1, bar_h, GB_COLOR_WHITE);
        }

        display_flush_force();
        odroid_display_unlock_gb_display();

        vTaskDelay(pdMS_TO_TICKS(80));
        odroid_gamepad_state state;
        odroid_input_gamepad_read(&state);
        bool changed = false;

        if (state.values[ODROID_INPUT_LEFT] && !prev.values[ODROID_INPUT_LEFT]) {
            if (level > 0) { level--; changed = true; }
            timeout = 25;
        }
        if (state.values[ODROID_INPUT_RIGHT] && !prev.values[ODROID_INPUT_RIGHT]) {
            if (level < ODROID_VOLUME_LEVEL_COUNT - 1) { level++; changed = true; }
            timeout = 25;
        }
        if (state.values[ODROID_INPUT_VOLUME] && !prev.values[ODROID_INPUT_VOLUME]) {
            level = (level + 1) % ODROID_VOLUME_LEVEL_COUNT;
            changed = true;
            timeout = 25;
        }
        if ((state.values[ODROID_INPUT_A] && !prev.values[ODROID_INPUT_A]) ||
            (state.values[ODROID_INPUT_B] && !prev.values[ODROID_INPUT_B])) {
            timeout = 0;
        }
        if (changed) {
            odroid_audio_volume_set(level);
            odroid_settings_Volume_set(level);
        }
        prev = state;
        timeout--;
    }
}

/* ─── Check / Delete save file for GB/GBC ──────────────────────── */
extern const char* SD_BASE_PATH;

static bool gb_check_save_exists(void)
{
    char *romPath = odroid_settings_RomFilePath_get();
    if (!romPath) return false;

    const char *fname = strrchr(romPath, '/');
    if (fname) fname++; else fname = romPath;

    /* Determine subdirectory from extension */
    const char *ext = strrchr(fname, '.');
    const char *subdir = "gb";
    if (ext && (strcasecmp(ext, ".gbc") == 0)) subdir = "gbc";

    /* Mount SD temporarily */
    esp_err_t r = odroid_sdcard_open(SD_BASE_PATH);
    if (r != ESP_OK) { free(romPath); return false; }

    /* Use full ROM filename (with extension) so path matches main menu:
     * e.g. /sd/odroid/data/gb/Pokemon.gb.sav */
    char pathBuf[512];
    snprintf(pathBuf, sizeof(pathBuf), "%s/odroid/data/%s/%s.sav", SD_BASE_PATH, subdir, fname);

    struct stat st;
    bool exists = (stat(pathBuf, &st) == 0);

    odroid_sdcard_close();
    free(romPath);
    return exists;
}

static bool gb_delete_save(void)
{
    char *romPath = odroid_settings_RomFilePath_get();
    if (!romPath) return false;

    const char *fname = strrchr(romPath, '/');
    if (fname) fname++; else fname = romPath;

    const char *ext = strrchr(fname, '.');
    const char *subdir = "gb";
    if (ext && (strcasecmp(ext, ".gbc") == 0)) subdir = "gbc";

    esp_err_t r = odroid_sdcard_open(SD_BASE_PATH);
    if (r != ESP_OK) { free(romPath); return false; }

    /* Use full ROM filename (with extension) to match main menu convention */
    char pathBuf[512];
    snprintf(pathBuf, sizeof(pathBuf), "%s/odroid/data/%s/%s.sav", SD_BASE_PATH, subdir, fname);

    struct stat st;
    bool ok = false;
    if (stat(pathBuf, &st) == 0) {
        ok = (unlink(pathBuf) == 0);
    }

    odroid_sdcard_close();
    free(romPath);
    return ok;
}

/* ─── In-Game Menu for GB/GBC ──────────────────────────────────── */
#define GB_MENU_RESUME     0
#define GB_MENU_RESTART    1
#define GB_MENU_SAVE       2
#define GB_MENU_RELOAD     3
#define GB_MENU_OVERWRITE  4
#define GB_MENU_DELETE     5
#define GB_MENU_EXIT       6

/* Returns: true = keep running, false = exit game */
static bool gb_show_ingame_menu(void)
{
    uint16_t *fb = display_get_framebuffer();
    if (!fb) return true;

    bool has_save = gb_check_save_exists();

    const char *labels[8];
    int ids[8];
    int count = 0;

    labels[count] = "Resume Game";      ids[count] = GB_MENU_RESUME;    count++;
    labels[count] = "Restart Game";     ids[count] = GB_MENU_RESTART;   count++;
    labels[count] = "Save Game";        ids[count] = GB_MENU_SAVE;      count++;
    if (has_save) {
        labels[count] = "Reload Game";  ids[count] = GB_MENU_RELOAD;    count++;
        labels[count] = "Overwrite Save"; ids[count] = GB_MENU_OVERWRITE; count++;
        labels[count] = "Delete Save";  ids[count] = GB_MENU_DELETE;    count++;
    }
    labels[count] = "Exit Game";        ids[count] = GB_MENU_EXIT;      count++;

    int selected = 0;
    odroid_gamepad_state prev;
    odroid_input_gamepad_read(&prev);

    /* Debounce: wait for MENU button release */
    for (int i = 0; i < 200; i++) {
        odroid_gamepad_state tmp;
        odroid_input_gamepad_read(&tmp);
        if (!tmp.values[ODROID_INPUT_MENU]) break;
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    odroid_input_gamepad_read(&prev);

    bool keep_running = true;
    bool menu_active = true;
    const char *flash_msg = NULL;
    int flash_timer = 0;

    while (menu_active) {
        odroid_display_lock_gb_display();

        int box_w = 160;
        int box_h = 20 + count * 14 + 10;
        int box_x = (320 - box_w) / 2;
        int box_y = (240 - box_h) / 2;

        gb_fill_rect(fb, box_x, box_y, box_w, box_h, GB_COLOR_BLACK);
        gb_fill_rect(fb, box_x, box_y, box_w, 1, GB_COLOR_WHITE);
        gb_fill_rect(fb, box_x, box_y + box_h - 1, box_w, 1, GB_COLOR_WHITE);
        gb_fill_rect(fb, box_x, box_y, 1, box_h, GB_COLOR_WHITE);
        gb_fill_rect(fb, box_x + box_w - 1, box_y, 1, box_h, GB_COLOR_WHITE);

        gb_draw_string(fb, box_x + (box_w - 9*6)/2, box_y + 5, "GAME MENU", GB_COLOR_YELLOW);

        for (int i = 0; i < count; i++) {
            int oy = box_y + 18 + i * 14;
            int ox = box_x + 16;
            uint16_t color = (i == selected) ? GB_COLOR_YELLOW : GB_COLOR_WHITE;
            gb_fill_rect(fb, box_x + 2, oy - 1, box_w - 4, 10, GB_COLOR_BLACK);
            if (i == selected) {
                gb_draw_char(fb, box_x + 6, oy, '>', GB_COLOR_YELLOW);
            }
            gb_draw_string(fb, ox, oy, labels[i], color);
        }

        if (flash_msg && flash_timer > 0) {
            int fw = strlen(flash_msg) * 6;
            int fx = box_x + (box_w - fw) / 2;
            int fy = box_y + box_h - 12;
            gb_fill_rect(fb, box_x + 2, fy - 2, box_w - 4, 12, GB_COLOR_BLACK);
            gb_draw_string(fb, fx, fy, flash_msg, GB_COLOR_GREEN);
            flash_timer--;
            if (flash_timer == 0) flash_msg = NULL;
        }

        display_flush_force();
        odroid_display_unlock_gb_display();

        vTaskDelay(pdMS_TO_TICKS(80));
        odroid_gamepad_state state;
        odroid_input_gamepad_read(&state);

        if (state.values[ODROID_INPUT_UP] && !prev.values[ODROID_INPUT_UP])
            selected = (selected - 1 + count) % count;
        if (state.values[ODROID_INPUT_DOWN] && !prev.values[ODROID_INPUT_DOWN])
            selected = (selected + 1) % count;

        /* B or MENU = resume */
        if ((state.values[ODROID_INPUT_B] && !prev.values[ODROID_INPUT_B]) ||
            (state.values[ODROID_INPUT_MENU] && !prev.values[ODROID_INPUT_MENU])) {
            menu_active = false;
            keep_running = true;
        }

        /* A = select option */
        if (state.values[ODROID_INPUT_A] && !prev.values[ODROID_INPUT_A]) {
            switch (ids[selected]) {
                case GB_MENU_RESUME:
                    menu_active = false;
                    keep_running = true;
                    break;

                case GB_MENU_RESTART:
                    printf("GB InGameMenu: Restart Game\n");
                    emu_reset();
                    lcd_begin();
                    sound_reset();
                    menu_active = false;
                    keep_running = true;
                    break;

                case GB_MENU_RELOAD:
                    printf("GB InGameMenu: Reload Game (full state)\n");
                    {
                        const char *ldPath = sram_get_savefile_path();
                        if (ldPath) {
                            FILE *lf = fopen(ldPath, "rb");
                            if (lf) {
                                loadstate(lf); fclose(lf);
                                vram_dirty(); pal_dirty(); sound_dirty(); mem_updatemap();
                            }
                        }
                    }
                    flash_msg = "Loaded!";
                    flash_timer = 15;
                    menu_active = false;
                    keep_running = true;
                    break;

                case GB_MENU_SAVE:
                    printf("GB InGameMenu: Save Game (full state)\n");
                    {
                        const char *savPath = sram_get_savefile_path();
                        if (savPath) {
                            FILE *sf = fopen(savPath, "wb");
                            if (sf) { savestate(sf); fclose(sf); printf("GB: full state saved to %s\n", savPath); }
                            else { printf("GB: failed to open %s for writing\n", savPath); }
                        }
                    }
                    if (!has_save) {
                        has_save = true;
                        count = 0;
                        labels[count] = "Resume Game";      ids[count] = GB_MENU_RESUME;    count++;
                        labels[count] = "Restart Game";     ids[count] = GB_MENU_RESTART;   count++;
                        labels[count] = "Save Game";        ids[count] = GB_MENU_SAVE;      count++;
                        labels[count] = "Reload Game";      ids[count] = GB_MENU_RELOAD;    count++;
                        labels[count] = "Overwrite Save";   ids[count] = GB_MENU_OVERWRITE; count++;
                        labels[count] = "Delete Save";      ids[count] = GB_MENU_DELETE;    count++;
                        labels[count] = "Exit Game";        ids[count] = GB_MENU_EXIT;      count++;
                    }
                    flash_msg = "Saved!";
                    flash_timer = 15;
                    break;

                case GB_MENU_OVERWRITE:
                    printf("GB InGameMenu: Overwrite Save (full state)\n");
                    {
                        const char *savPath2 = sram_get_savefile_path();
                        if (savPath2) {
                            FILE *sf2 = fopen(savPath2, "wb");
                            if (sf2) { savestate(sf2); fclose(sf2); }
                        }
                    }
                    flash_msg = "Saved!";
                    flash_timer = 15;
                    break;

                case GB_MENU_DELETE:
                    printf("GB InGameMenu: Delete Save\n");
                    if (gb_delete_save()) {
                        has_save = false;
                        flash_msg = "Deleted!";
                        flash_timer = 15;
                        count = 0;
                        labels[count] = "Resume Game";      ids[count] = GB_MENU_RESUME;    count++;
                        labels[count] = "Restart Game";     ids[count] = GB_MENU_RESTART;   count++;
                        labels[count] = "Save Game";        ids[count] = GB_MENU_SAVE;      count++;
                        labels[count] = "Exit Game";        ids[count] = GB_MENU_EXIT;      count++;
                        if (selected >= count) selected = count - 1;
                    } else {
                        flash_msg = "Error!";
                        flash_timer = 15;
                    }
                    break;

                case GB_MENU_EXIT:
                    printf("GB InGameMenu: Exit Game — auto-saving full state\n");
                    {
                        const char *exitSavPath = sram_get_savefile_path();
                        if (exitSavPath) {
                            FILE *esf = fopen(exitSavPath, "wb");
                            if (esf) { savestate(esf); fclose(esf); printf("GB: state saved on exit to %s\n", exitSavPath); }
                        }
                    }
                    menu_active = false;
                    keep_running = false;
                    break;
            }
        }

        prev = state;
    }

    return keep_running;
}

/* --- State --- */
struct fb fb;
struct pcm pcm;

int frame = 0;  /* frame counter used by lcd.c */
uint16_t* displayBuffer[2];
static uint8_t currentBuffer;
static uint16_t* framebuffer_ptr;

static int32_t* audioBuffer[2];
static volatile uint8_t currentAudioBuffer;
static volatile uint16_t currentAudioSampleCount;
static volatile int16_t* currentAudioBufferPtr;

static QueueHandle_t vidQueue;
static QueueHandle_t audioQueue;

static volatile bool videoTaskIsRunning = false;
static volatile bool audioTaskIsRunning = false;
static volatile bool quit_flag = false;

/* --- PCM submit callback (called by gnuboy sound) --- */
int pcm_submit(void)
{
    odroid_audio_submit((short*)currentAudioBufferPtr, currentAudioSampleCount >> 1);
    return 1;
}

/* --- Run one frame --- */
static void run_to_vblank(void)
{
    cpu_emulate(2280);

    while (R_LY > 0 && R_LY < 144)
    {
        emu_step();
    }

    /* VBLANK: send video */
    xQueueSend(vidQueue, &framebuffer_ptr, portMAX_DELAY);

    currentBuffer = currentBuffer ? 0 : 1;
    framebuffer_ptr = displayBuffer[currentBuffer];
    fb.ptr = framebuffer_ptr;

    rtc_tick();

    /* Audio */
    sound_mix();

    currentAudioBufferPtr = (int16_t*)audioBuffer[currentAudioBuffer];
    currentAudioSampleCount = pcm.pos;

    void* tempPtr = (void*)0x1234;
    xQueueSend(audioQueue, &tempPtr, portMAX_DELAY);

    currentAudioBuffer = currentAudioBuffer ? 0 : 1;
    pcm.buf = audioBuffer[currentAudioBuffer];
    pcm.pos = 0;

    if (!(R_LCDC & 0x80)) {
        cpu_emulate(32832);
    }

    while (R_LY > 0) {
        emu_step();
    }
}

/* --- Video task (runs on core 1) --- */
static void videoTask(void *arg)
{
    videoTaskIsRunning = true;
    uint16_t* param;

    while (1)
    {
        xQueuePeek(vidQueue, &param, portMAX_DELAY);

        if (param == (uint16_t*)1)
            break;

        ili9341_write_frame_gb(param, false);

        xQueueReceive(vidQueue, &param, portMAX_DELAY);
    }

    videoTaskIsRunning = false;
    vTaskDelete(NULL);
}

/* --- Audio task --- */
static void audioTask(void *arg)
{
    audioTaskIsRunning = true;
    uint16_t* param;

    while (1)
    {
        xQueuePeek(audioQueue, &param, portMAX_DELAY);

        if (param == (uint16_t*)1)
            break;

        pcm_submit();

        xQueueReceive(audioQueue, &param, portMAX_DELAY);
    }

    odroid_audio_terminate();
    audioTaskIsRunning = false;
    vTaskDelete(NULL);
}

/* ================================================================ */
void gnuboy_run(const char *rom_path)
{
    ESP_LOGI(TAG, "Starting gnuboy with ROM: %s", rom_path);
    quit_flag = false;

    /* Set ROM path in settings (gnuboy loader reads it) */
    odroid_settings_RomFilePath_set(rom_path);

    /* Prepare display */
    ili9341_prepare();
    ili9341_write_frame_gb(NULL, true);

    /* Audio */
    odroid_audio_init(AUDIO_SAMPLE_RATE);

    /* Allocate display buffers in PSRAM */
    /* DMA-capable + cache-aligned for PPA hardware scaling */
    displayBuffer[0] = heap_caps_aligned_alloc(64, GAMEBOY_WIDTH * GAMEBOY_HEIGHT * 2,
                                                MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA);
    displayBuffer[1] = heap_caps_aligned_alloc(64, GAMEBOY_WIDTH * GAMEBOY_HEIGHT * 2,
                                                MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA);
    if (!displayBuffer[0] || !displayBuffer[1]) {
        ESP_LOGE(TAG, "Failed to allocate display buffers!");
        return;
    }
    memset(displayBuffer[0], 0, GAMEBOY_WIDTH * GAMEBOY_HEIGHT * 2);
    memset(displayBuffer[1], 0, GAMEBOY_WIDTH * GAMEBOY_HEIGHT * 2);

    currentBuffer = 0;
    framebuffer_ptr = displayBuffer[0];

    /* Audio buffers */
    const int audioBufferLength = AUDIO_SAMPLE_RATE / 10 + 1;
    const int AUDIO_BUFFER_SIZE = audioBufferLength * sizeof(int16_t) * 2;

    memset(&pcm, 0, sizeof(pcm));
    pcm.hz = AUDIO_SAMPLE_RATE;
    pcm.stereo = 1;
    pcm.len = audioBufferLength;
    pcm.buf = heap_caps_malloc(AUDIO_BUFFER_SIZE, MALLOC_CAP_SPIRAM);
    pcm.pos = 0;

    audioBuffer[0] = pcm.buf;
    audioBuffer[1] = heap_caps_malloc(AUDIO_BUFFER_SIZE, MALLOC_CAP_SPIRAM);

    if (!audioBuffer[0] || !audioBuffer[1]) {
        ESP_LOGE(TAG, "Failed to allocate audio buffers!");
        goto cleanup;
    }

    currentAudioBuffer = 0;

    /* Set SD base path for save file creation */
    extern const char* SD_BASE_PATH;
    SD_BASE_PATH = "/sd";

    /* Load ROM */
    loader_init(NULL);

    /* Clear display */
    ili9341_write_frame_gb(NULL, true);

    /* Setup gnuboy framebuffer struct */
    memset(&fb, 0, sizeof(fb));
    fb.w = GAMEBOY_WIDTH;
    fb.h = GAMEBOY_HEIGHT;
    fb.pelsize = 2;
    fb.pitch = fb.w * fb.pelsize;
    fb.indexed = 0;
    fb.ptr = framebuffer_ptr;
    fb.enabled = 1;
    fb.dirty = 0;

    /* Create tasks */
    vidQueue = xQueueCreate(1, sizeof(uint16_t*));
    audioQueue = xQueueCreate(1, sizeof(uint16_t*));

    xTaskCreatePinnedToCore(&videoTask, "gb_video", 4096, NULL, 5, NULL, 1);
    xTaskCreatePinnedToCore(&audioTask, "gb_audio", 4096, NULL, 5, NULL, 1);

    /* Reset emulator (baseline init) */
    emu_reset();

    rtc.d = 1;
    rtc.h = 1;
    rtc.m = 1;
    rtc.s = 1;
    rtc.t = 1;

    sound_reset();
    lcd_begin();

    /* Check for resume: if StartAction is RESTART, load full emulator state */
    bool resumed = false;
    if (odroid_settings_StartAction_get() == ODROID_START_ACTION_RESTART)
    {
        odroid_settings_StartAction_set(ODROID_START_ACTION_NORMAL);
        const char *savPath = sram_get_savefile_path();
        if (savPath) {
            FILE *sf = fopen(savPath, "rb");
            if (sf) {
                ESP_LOGI(TAG, "Resuming: loading full state from %s", savPath);
                loadstate(sf);
                fclose(sf);
                vram_dirty();
                pal_dirty();
                sound_dirty();
                mem_updatemap();
                resumed = true;
                ESP_LOGI(TAG, "Resume state loaded successfully");
            } else {
                ESP_LOGW(TAG, "Resume: save file not found: %s", savPath);
            }
        }
    }
    if (!resumed) {
        ESP_LOGI(TAG, "Fresh start (no resume)");
    }

    /* Main emulation loop */
    odroid_gamepad_state lastJoystickState;
    odroid_input_gamepad_read(&lastJoystickState);
    bool ignoreMenuButton = lastJoystickState.values[ODROID_INPUT_MENU];
    uint16_t menuButtonFrameCount = 0;
    frame = 0;  /* reset global frame counter (used by lcd.c) — do NOT redeclare */
    uint64_t totalElapsedUs = 0;
    int actualFrameCount = 0;

    while (!quit_flag)
    {
        odroid_gamepad_state joystick;
        odroid_input_gamepad_read(&joystick);

        if (ignoreMenuButton)
            ignoreMenuButton = lastJoystickState.values[ODROID_INPUT_MENU];

        if (!ignoreMenuButton && lastJoystickState.values[ODROID_INPUT_MENU] && joystick.values[ODROID_INPUT_MENU])
            ++menuButtonFrameCount;
        else
            menuButtonFrameCount = 0;

        /* Short MENU press: show in-game menu */
        if (!ignoreMenuButton && lastJoystickState.values[ODROID_INPUT_MENU] && !joystick.values[ODROID_INPUT_MENU])
        {
            ESP_LOGI(TAG, "MENU pressed — opening in-game menu");
            if (!gb_show_ingame_menu()) {
                ESP_LOGI(TAG, "User chose Exit — returning to launcher");
                quit_flag = true;
                break;
            }
            /* Re-read gamepad state after menu closes */
            odroid_input_gamepad_read(&joystick);
        }

        /* Volume button: show volume overlay */
        if (!lastJoystickState.values[ODROID_INPUT_VOLUME] && joystick.values[ODROID_INPUT_VOLUME])
        {
            gb_show_volume_overlay();
            ESP_LOGI(TAG, "Volume: %d", odroid_audio_volume_get());
            /* Re-read gamepad state after overlay closes */
            odroid_input_gamepad_read(&joystick);
        }

        /* Gamepad to emulator input */
        pad_set(PAD_UP, joystick.values[ODROID_INPUT_UP]);
        pad_set(PAD_RIGHT, joystick.values[ODROID_INPUT_RIGHT]);
        pad_set(PAD_DOWN, joystick.values[ODROID_INPUT_DOWN]);
        pad_set(PAD_LEFT, joystick.values[ODROID_INPUT_LEFT]);
        pad_set(PAD_SELECT, joystick.values[ODROID_INPUT_SELECT]);
        pad_set(PAD_START, joystick.values[ODROID_INPUT_START]);
        pad_set(PAD_A, joystick.values[ODROID_INPUT_A]);
        pad_set(PAD_B, joystick.values[ODROID_INPUT_B]);

        /* Run one frame */
        int64_t startUs = esp_timer_get_time();
        run_to_vblank();
        int64_t endUs = esp_timer_get_time();

        lastJoystickState = joystick;

        totalElapsedUs += (endUs - startUs);
        ++frame;
        ++actualFrameCount;

        if (actualFrameCount == 60)
        {
            float seconds = totalElapsedUs / 1000000.0f;
            float fps = actualFrameCount / seconds;
            ESP_LOGI(TAG, "HEAP:0x%x, FPS:%.1f", (unsigned)esp_get_free_heap_size(), fps);
            actualFrameCount = 0;
            totalElapsedUs = 0;
        }
    }

    /* --- Shutdown --- */
    ESP_LOGI(TAG, "Shutting down gnuboy...");

    /* Stop audio */
    {
        uint16_t* discard;
        while (xQueueReceive(audioQueue, &discard, 0) == pdTRUE) {}
        uint16_t* param = (uint16_t*)1;
        xQueueOverwrite(audioQueue, &param);
        int timeout = 500;
        while (audioTaskIsRunning && --timeout > 0) vTaskDelay(1);
    }

    /* Stop video */
    {
        uint16_t* discard;
        while (xQueueReceive(vidQueue, &discard, 0) == pdTRUE) {}
        uint16_t* param = (uint16_t*)1;
        xQueueOverwrite(vidQueue, &param);
        int timeout = 500;
        while (videoTaskIsRunning && --timeout > 0) vTaskDelay(1);
    }

cleanup:
    /* Unload ROM, free SRAM, save battery-backed RAM */
    loader_unload();

    /* Close ROM file handle kept open for on-demand bank loading */
    {
        extern FILE* RomFile;
        extern uint8_t BankCache[];
        if (RomFile) { fclose(RomFile); RomFile = NULL; }
        memset(BankCache, 0, sizeof(uint8_t) * (512 / 8));
    }

    /* Free resources */
    if (vidQueue) { vQueueDelete(vidQueue); vidQueue = NULL; }
    if (audioQueue) { vQueueDelete(audioQueue); audioQueue = NULL; }

    if (displayBuffer[0]) { heap_caps_free(displayBuffer[0]); displayBuffer[0] = NULL; }
    if (displayBuffer[1]) { heap_caps_free(displayBuffer[1]); displayBuffer[1] = NULL; }
    if (audioBuffer[0]) { heap_caps_free(audioBuffer[0]); audioBuffer[0] = NULL; }
    if (audioBuffer[1]) { heap_caps_free(audioBuffer[1]); audioBuffer[1] = NULL; }
    pcm.buf = NULL;

    /* Clear display */
    ili9341_clear(0x0000);
    display_flush_force();

    ESP_LOGI(TAG, "gnuboy exited cleanly");
}
