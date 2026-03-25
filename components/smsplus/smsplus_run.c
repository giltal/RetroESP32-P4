/*
 * smsplus_run.c — Entry point for SMS/GG (smsplus) emulator on ESP32-P4
 *
 * Adapted from the original smsplusgx-go main.c.
 * Loads a ROM, runs the SMS/GG emulation loop, returns on MENU press.
 */

#include "smsplus_run.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_heap_caps.h"
#include "esp_system.h"
#include "esp_timer.h"

#include "shared.h"

#include "odroid_settings.h"
#include "odroid_audio.h"
#include "odroid_input.h"
#include "odroid_system.h"
#include "odroid_display.h"
#include "odroid_sdcard.h"

#define AUDIO_SAMPLE_RATE 32000

/* Global PSRAM pointer — replaces hardcoded 0x3f800000 */
uint8 *ESP32_PSRAM = NULL;

static const char *SD_BASE_PATH = "/sd";

static uint16 palette[PALETTE_SIZE];
static uint8_t *framebuffer[2];
static int currentFramebuffer = 0;

/* Display copy buffers — decouples emulator from video task (like NES lcdfb) */
static uint8_t *lcdfb[2] = { NULL, NULL };
static int lcdfb_write_idx = 0;

static uint32_t *audioBuffer = NULL;
static int audioBufferCount = 0;

static QueueHandle_t vidQueue;
static TaskHandle_t videoTaskHandle;

static odroid_volume_level Volume;
static odroid_battery_state battery;

static bool scaling_enabled = true;
static bool previous_scaling_enabled = true;

static volatile bool videoTaskIsRunning = false;
static volatile bool smsplus_quit_flag = false;

/* ─── 5×7 Bitmap Font (space, A-Z, !>.,:%, 0-9, a-z) ─────────── */
static const uint8_t sms_font5x7[][7] = {
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
    /* ',' */ {0x00,0x00,0x00,0x00,0x00,0x04,0x08},
    /* ':' */ {0x00,0x04,0x00,0x00,0x00,0x04,0x00},
    /* '%' */ {0x18,0x19,0x02,0x04,0x08,0x13,0x03},
    /* '0' */ {0x0E,0x11,0x13,0x15,0x19,0x11,0x0E},
    /* '1' */ {0x04,0x0C,0x04,0x04,0x04,0x04,0x0E},
    /* '2' */ {0x0E,0x11,0x01,0x02,0x04,0x08,0x1F},
    /* '3' */ {0x0E,0x11,0x01,0x06,0x01,0x11,0x0E},
    /* '4' */ {0x02,0x06,0x0A,0x12,0x1F,0x02,0x02},
    /* '5' */ {0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E},
    /* '6' */ {0x06,0x08,0x10,0x1E,0x11,0x11,0x0E},
    /* '7' */ {0x1F,0x01,0x02,0x04,0x08,0x08,0x08},
    /* '8' */ {0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E},
    /* '9' */ {0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C},
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

static int sms_font_index(char c)
{
    if (c == ' ')  return 0;
    if (c >= 'A' && c <= 'Z') return 1 + (c - 'A');
    if (c == '!')  return 27;
    if (c == '>')  return 28;
    if (c == '.')  return 29;
    if (c == ',')  return 30;
    if (c == ':')  return 31;
    if (c == '%')  return 32;
    if (c >= '0' && c <= '9') return 33 + (c - '0');
    if (c >= 'a' && c <= 'z') return 43 + (c - 'a');
    return 0;
}

/* RGB565 colors for overlay UI */
#define SMS_COLOR_BLACK     0x0000
#define SMS_COLOR_WHITE     0xFFFF
#define SMS_COLOR_YELLOW    0xE0FF
#define SMS_COLOR_DKGRAY    0x2108
#define SMS_COLOR_GREEN     0xE007
#define SMS_COLOR_CYAN      0xFF07

static void sms_draw_char(uint16_t *fb, int px, int py, char c, uint16_t color)
{
    int idx = sms_font_index(c);
    const uint8_t *glyph = sms_font5x7[idx];
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

static void sms_draw_string(uint16_t *fb, int px, int py, const char *str, uint16_t color)
{
    while (*str) {
        sms_draw_char(fb, px, py, *str, color);
        px += 6;
        str++;
    }
}

static void sms_fill_rect(uint16_t *fb, int x, int y, int w, int h, uint16_t color)
{
    for (int row = y; row < y + h && row < 240; row++) {
        if (row < 0) continue;
        for (int col = x; col < x + w && col < 320; col++) {
            if (col < 0) continue;
            fb[row * 320 + col] = color;
        }
    }
}

/* Forward declarations for save/load used by in-game menu */
static void SaveState(void);
static void LoadState(void);

/* ─── Volume Overlay for SMS/GG ──────────────────────────────────── */
static void sms_show_volume_overlay(void)
{
    uint16_t *fb = display_get_emu_buffer();
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
        odroid_display_lock_sms_display();

        int box_w = 140, box_h = 34;
        int box_x = (320 - box_w) / 2, box_y = 8;

        sms_fill_rect(fb, box_x, box_y, box_w, box_h, SMS_COLOR_BLACK);
        sms_fill_rect(fb, box_x, box_y, box_w, 1, SMS_COLOR_WHITE);
        sms_fill_rect(fb, box_x, box_y + box_h - 1, box_w, 1, SMS_COLOR_WHITE);
        sms_fill_rect(fb, box_x, box_y, 1, box_h, SMS_COLOR_WHITE);
        sms_fill_rect(fb, box_x + box_w - 1, box_y, 1, box_h, SMS_COLOR_WHITE);

        char title[32];
        snprintf(title, sizeof(title), "VOLUME: %s", level_names[level]);
        int title_w = strlen(title) * 6;
        sms_draw_string(fb, box_x + (box_w - title_w) / 2, box_y + 4, title, SMS_COLOR_YELLOW);

        int bar_x = box_x + 10, bar_y = box_y + 16, bar_w = box_w - 20, bar_h = 10;
        sms_fill_rect(fb, bar_x, bar_y, bar_w, bar_h, SMS_COLOR_DKGRAY);
        if (level > 0) {
            int fill_w = (bar_w * level) / (ODROID_VOLUME_LEVEL_COUNT - 1);
            uint16_t bar_color = (level <= 1) ? SMS_COLOR_GREEN :
                                 (level <= 3) ? SMS_COLOR_CYAN : SMS_COLOR_YELLOW;
            sms_fill_rect(fb, bar_x, bar_y, fill_w, bar_h, bar_color);
        }
        sms_fill_rect(fb, bar_x, bar_y, bar_w, 1, SMS_COLOR_WHITE);
        sms_fill_rect(fb, bar_x, bar_y + bar_h - 1, bar_w, 1, SMS_COLOR_WHITE);
        sms_fill_rect(fb, bar_x, bar_y, 1, bar_h, SMS_COLOR_WHITE);
        sms_fill_rect(fb, bar_x + bar_w - 1, bar_y, 1, bar_h, SMS_COLOR_WHITE);
        for (int i = 1; i < ODROID_VOLUME_LEVEL_COUNT - 1; i++) {
            int sx = bar_x + (bar_w * i) / (ODROID_VOLUME_LEVEL_COUNT - 1);
            sms_fill_rect(fb, sx, bar_y, 1, bar_h, SMS_COLOR_WHITE);
        }

        display_emu_flush();
        odroid_display_unlock_sms_display();

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

/* ─── Save-subdirectory from ROM extension (sms / gg / col) ──── */
static const char *sms_get_save_subdir(void)
{
    char *romName = odroid_settings_RomFilePath_get();
    if (!romName) return "sms";

    const char *ext = strrchr(romName, '.');
    const char *subdir = "sms";
    if (ext) {
        if (strcasecmp(ext, ".gg") == 0)  subdir = "gg";
        else if (strcasecmp(ext, ".col") == 0) subdir = "col";
    }
    free(romName);
    return subdir;
}

/* ─── Check / Delete save file for SMS/GG/COL ──────────────────── */
static char *sms_get_save_path(void)
{
    char *romName = odroid_settings_RomFilePath_get();
    if (!romName) return NULL;

    char *fileName = odroid_util_GetFileName(romName);
    free(romName);
    if (!fileName) return NULL;

    const char *subdir = sms_get_save_subdir();

    /* Build path: /sd/odroid/data/<subdir>/<fileName>.sav */
    char pathBuf[512];
    snprintf(pathBuf, sizeof(pathBuf), "%s/odroid/data/%s/%s.sav", SD_BASE_PATH, subdir, fileName);
    free(fileName);

    char *pathName = strdup(pathBuf);
    return pathName;
}

static bool sms_check_save_exists(void)
{
    char *path = sms_get_save_path();
    if (!path) return false;

    struct stat st;
    bool exists = (stat(path, &st) == 0);
    free(path);
    return exists;
}

static bool sms_delete_save(void)
{
    char *path = sms_get_save_path();
    if (!path) return false;

    struct stat st;
    bool ok = false;
    if (stat(path, &st) == 0) {
        ok = (unlink(path) == 0);
    }
    free(path);
    return ok;
}

/* ─── In-Game Menu for SMS/GG ──────────────────────────────────── */
#define SMS_MENU_RESUME     0
#define SMS_MENU_RESTART    1
#define SMS_MENU_SAVE       2
#define SMS_MENU_RELOAD     3
#define SMS_MENU_OVERWRITE  4
#define SMS_MENU_DELETE     5
#define SMS_MENU_EXIT       6

/* Returns: true = keep running, false = exit game */
static bool sms_show_ingame_menu(void)
{
    uint16_t *fb = display_get_emu_buffer();
    if (!fb) return true;

    bool has_save = sms_check_save_exists();

    const char *labels[8];
    int ids[8];
    int count = 0;

    labels[count] = "Resume Game";      ids[count] = SMS_MENU_RESUME;    count++;
    labels[count] = "Restart Game";     ids[count] = SMS_MENU_RESTART;   count++;
    labels[count] = "Save Game";        ids[count] = SMS_MENU_SAVE;      count++;
    if (has_save) {
        labels[count] = "Reload Game";  ids[count] = SMS_MENU_RELOAD;    count++;
        labels[count] = "Overwrite Save"; ids[count] = SMS_MENU_OVERWRITE; count++;
        labels[count] = "Delete Save";  ids[count] = SMS_MENU_DELETE;    count++;
    }
    labels[count] = "Exit Game";        ids[count] = SMS_MENU_EXIT;      count++;

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
        odroid_display_lock_sms_display();

        int box_w = 160;
        int box_h = 20 + count * 14 + 10;
        int box_x = (320 - box_w) / 2;
        int box_y = (240 - box_h) / 2;

        sms_fill_rect(fb, box_x, box_y, box_w, box_h, SMS_COLOR_BLACK);
        sms_fill_rect(fb, box_x, box_y, box_w, 1, SMS_COLOR_WHITE);
        sms_fill_rect(fb, box_x, box_y + box_h - 1, box_w, 1, SMS_COLOR_WHITE);
        sms_fill_rect(fb, box_x, box_y, 1, box_h, SMS_COLOR_WHITE);
        sms_fill_rect(fb, box_x + box_w - 1, box_y, 1, box_h, SMS_COLOR_WHITE);

        sms_draw_string(fb, box_x + (box_w - 9*6)/2, box_y + 5, "GAME MENU", SMS_COLOR_YELLOW);

        for (int i = 0; i < count; i++) {
            int oy = box_y + 18 + i * 14;
            int ox = box_x + 16;
            uint16_t color = (i == selected) ? SMS_COLOR_YELLOW : SMS_COLOR_WHITE;
            sms_fill_rect(fb, box_x + 2, oy - 1, box_w - 4, 10, SMS_COLOR_BLACK);
            if (i == selected) {
                sms_draw_char(fb, box_x + 6, oy, '>', SMS_COLOR_YELLOW);
            }
            sms_draw_string(fb, ox, oy, labels[i], color);
        }

        if (flash_msg && flash_timer > 0) {
            int fw = strlen(flash_msg) * 6;
            int fx = box_x + (box_w - fw) / 2;
            int fy = box_y + box_h - 12;
            sms_fill_rect(fb, box_x + 2, fy - 2, box_w - 4, 12, SMS_COLOR_BLACK);
            sms_draw_string(fb, fx, fy, flash_msg, SMS_COLOR_GREEN);
            flash_timer--;
            if (flash_timer == 0) flash_msg = NULL;
        }

        display_emu_flush();
        odroid_display_unlock_sms_display();

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
                case SMS_MENU_RESUME:
                    menu_active = false;
                    keep_running = true;
                    break;

                case SMS_MENU_RESTART:
                    printf("SMS InGameMenu: Restart Game\n");
                    system_reset();
                    menu_active = false;
                    keep_running = true;
                    break;

                case SMS_MENU_RELOAD:
                    printf("SMS InGameMenu: Reload Game\n");
                    LoadState();
                    flash_msg = "Loaded!";
                    flash_timer = 15;
                    menu_active = false;
                    keep_running = true;
                    break;

                case SMS_MENU_SAVE:
                    printf("SMS InGameMenu: Save Game\n");
                    SaveState();
                    if (!has_save) {
                        has_save = true;
                        count = 0;
                        labels[count] = "Resume Game";      ids[count] = SMS_MENU_RESUME;    count++;
                        labels[count] = "Restart Game";     ids[count] = SMS_MENU_RESTART;   count++;
                        labels[count] = "Save Game";        ids[count] = SMS_MENU_SAVE;      count++;
                        labels[count] = "Reload Game";      ids[count] = SMS_MENU_RELOAD;    count++;
                        labels[count] = "Overwrite Save";   ids[count] = SMS_MENU_OVERWRITE; count++;
                        labels[count] = "Delete Save";      ids[count] = SMS_MENU_DELETE;    count++;
                        labels[count] = "Exit Game";        ids[count] = SMS_MENU_EXIT;      count++;
                    }
                    flash_msg = "Saved!";
                    flash_timer = 15;
                    break;

                case SMS_MENU_OVERWRITE:
                    printf("SMS InGameMenu: Overwrite Save\n");
                    SaveState();
                    flash_msg = "Saved!";
                    flash_timer = 15;
                    break;

                case SMS_MENU_DELETE:
                    printf("SMS InGameMenu: Delete Save\n");
                    if (sms_delete_save()) {
                        has_save = false;
                        flash_msg = "Deleted!";
                        flash_timer = 15;
                        count = 0;
                        labels[count] = "Resume Game";      ids[count] = SMS_MENU_RESUME;    count++;
                        labels[count] = "Restart Game";     ids[count] = SMS_MENU_RESTART;   count++;
                        labels[count] = "Save Game";        ids[count] = SMS_MENU_SAVE;      count++;
                        labels[count] = "Exit Game";        ids[count] = SMS_MENU_EXIT;      count++;
                        if (selected >= count) selected = count - 1;
                    } else {
                        flash_msg = "Error!";
                        flash_timer = 15;
                    }
                    break;

                case SMS_MENU_EXIT:
                    printf("SMS InGameMenu: Exit Game\n");
                    menu_active = false;
                    keep_running = false;
                    break;
            }
        }

        prev = state;
    }

    return keep_running;
}

/* ---- Video Task ---- */
static void videoTask(void *arg)
{
    uint8_t *param;
    videoTaskIsRunning = true;

    const bool isGameGear = (sms.console == CONSOLE_GG) || (sms.console == CONSOLE_GGMS);

    while (1)
    {
        xQueuePeek(vidQueue, &param, portMAX_DELAY);

        if ((uintptr_t)param == 1)
            break;

        if (previous_scaling_enabled != scaling_enabled)
        {
            ili9341_write_frame_sms(NULL, NULL, isGameGear, false);
            previous_scaling_enabled = scaling_enabled;
        }

        render_copy_palette(palette);
        ili9341_write_frame_sms(param, palette, isGameGear, scaling_enabled);

        odroid_input_battery_level_read(&battery);

        xQueueReceive(vidQueue, &param, portMAX_DELAY);
    }

    odroid_display_lock_sms_display();
    odroid_display_show_hourglass();
    odroid_display_unlock_sms_display();

    videoTaskIsRunning = false;
    vTaskDelete(NULL);
    while (1) {}
}

/* ---- Save / Load State ---- */
static void SaveState(void)
{
    odroid_input_battery_monitor_enabled_set(0);
    odroid_system_led_set(1);

    char *romName = odroid_settings_RomFilePath_get();
    if (romName)
    {
        odroid_display_lock_sms_display();
        odroid_display_drain_spi();

        char *fileName = odroid_util_GetFileName(romName);
        if (!fileName) { free(romName); goto done; }

        const char *subdir = sms_get_save_subdir();

        /* Create save directory tree: /sd/odroid/data/<subdir>/ */
        char dirBuf[512];
        snprintf(dirBuf, sizeof(dirBuf), "%s/odroid", SD_BASE_PATH);
        mkdir(dirBuf, 0775);
        snprintf(dirBuf, sizeof(dirBuf), "%s/odroid/data", SD_BASE_PATH);
        mkdir(dirBuf, 0775);
        snprintf(dirBuf, sizeof(dirBuf), "%s/odroid/data/%s", SD_BASE_PATH, subdir);
        mkdir(dirBuf, 0775);

        /* Save to: /sd/odroid/data/<subdir>/<fileName>.sav */
        char pathBuf[512];
        snprintf(pathBuf, sizeof(pathBuf), "%s/odroid/data/%s/%s.sav", SD_BASE_PATH, subdir, fileName);
        printf("SaveState: saving to '%s'\n", pathBuf);

        FILE *f = fopen(pathBuf, "w");
        if (f)
        {
            system_save_state(f);
            fclose(f);
            printf("SaveState: OK.\n");
        }
        else
        {
            printf("SaveState: fopen failed\n");
        }

        odroid_display_unlock_sms_display();
        free(fileName);
        free(romName);
    }

done:
    odroid_system_led_set(0);
    odroid_input_battery_monitor_enabled_set(1);
}

static void LoadState(void)
{
    char *romName = odroid_settings_RomFilePath_get();
    if (romName)
    {
        odroid_display_lock_sms_display();
        odroid_display_drain_spi();

        char *fileName = odroid_util_GetFileName(romName);
        if (!fileName) { free(romName); return; }

        const char *subdir = sms_get_save_subdir();

        /* Load from: /sd/odroid/data/<subdir>/<fileName>.sav */
        char pathBuf[512];
        snprintf(pathBuf, sizeof(pathBuf), "%s/odroid/data/%s/%s.sav", SD_BASE_PATH, subdir, fileName);
        printf("LoadState: loading from '%s'\n", pathBuf);

        FILE *f = fopen(pathBuf, "r");
        if (f)
        {
            system_load_state(f);
            fclose(f);
            printf("LoadState: OK.\n");
        }
        else
        {
            printf("LoadState: no save file found.\n");
        }

        odroid_display_unlock_sms_display();
        free(fileName);
        free(romName);
    }

    Volume = odroid_settings_Volume_get();
}

static void DoQuit(void)
{
    printf("DoQuit: stopping audio.\n");
    odroid_audio_terminate();

    printf("DoQuit: stopping video task.\n");
    {
        uint8_t *discard;
        while (xQueueReceive(vidQueue, &discard, 0) == pdTRUE) {}
        uint8_t *param = (uint8_t *)(uintptr_t)1;
        xQueueOverwrite(vidQueue, &param);
        int timeout = 500;
        while (videoTaskIsRunning && --timeout > 0) { vTaskDelay(1); }
    }

    printf("DoQuit: saving state.\n");
    SaveState();

    smsplus_quit_flag = true;
}

/* ---- Callback required by smsplus ---- */
void system_manage_sram(uint8 *sram, int slot, int mode)
{
    printf("system_manage_sram\n");
}

/* ---- Main entry point ---- */
void smsplus_run(const char *rom_path)
{
    printf("smsplus_run: starting SMS/GG emulator, ROM=%s\n", rom_path);

    smsplus_quit_flag = false;

    /* Store ROM path for save/load */
    odroid_settings_RomFilePath_set(rom_path);

    /* Allocate PSRAM buffer (2MB) for ROM + coleco BIOS */
    ESP32_PSRAM = (uint8 *)heap_caps_malloc(0x200000, MALLOC_CAP_SPIRAM);
    if (!ESP32_PSRAM)
    {
        printf("smsplus_run: ERROR — PSRAM alloc failed\n");
        return;
    }

    /* Allocate double-buffered framebuffers */
    framebuffer[0] = heap_caps_malloc(256 * 192, MALLOC_CAP_SPIRAM);
    framebuffer[1] = heap_caps_malloc(256 * 192, MALLOC_CAP_SPIRAM);
    if (!framebuffer[0] || !framebuffer[1])
    {
        printf("smsplus_run: ERROR — framebuffer alloc failed\n");
        goto cleanup;
    }

    /* Display copy buffers for non-blocking video (like NES lcdfb) */
    lcdfb[0] = heap_caps_malloc(256 * 192, MALLOC_CAP_SPIRAM);
    lcdfb[1] = heap_caps_malloc(256 * 192, MALLOC_CAP_SPIRAM);
    if (!lcdfb[0] || !lcdfb[1])
    {
        printf("smsplus_run: ERROR — lcdfb alloc failed\n");
        goto cleanup;
    }
    lcdfb_write_idx = 0;

    /* Open SD card and load ROM */
    esp_err_t r = odroid_sdcard_open("/sd");
    if (r != ESP_OK)
    {
        printf("smsplus_run: ERROR — SD open failed\n");
        goto cleanup;
    }

    load_rom((char *)rom_path);

    /* Set up display */
    const bool isGameGear = (sms.console == CONSOLE_GG) || (sms.console == CONSOLE_GGMS);
    ili9341_write_frame_sms(NULL, NULL, isGameGear, false);

    /* Audio init */
    odroid_audio_init(AUDIO_SAMPLE_RATE);

    /* Video queue and task */
    vidQueue = xQueueCreate(1, sizeof(uint8_t *));
    xTaskCreatePinnedToCore(&videoTask, "sms_video", 1024 * 4, NULL, 5, &videoTaskHandle, 1);

    /* Emulator init */
    sms.use_fm = 0;

    bitmap.width = 256;
    bitmap.height = 192;
    bitmap.pitch = bitmap.width;
    bitmap.data = framebuffer[0];
    currentFramebuffer = 0;

    set_option_defaults();
    option.sndrate = AUDIO_SAMPLE_RATE;
    option.overscan = 0;
    option.extra_gg = 0;

    system_init2();
    system_reset();

    /* Check for resume: if StartAction is RESTART, load saved state */
    if (odroid_settings_StartAction_get() == ODROID_START_ACTION_RESTART)
    {
        odroid_settings_StartAction_set(ODROID_START_ACTION_NORMAL);
        LoadState();
        printf("smsplus_run: resumed from save state\n");
    }

    /* Input state */
    odroid_gamepad_state previousState;
    odroid_input_gamepad_read(&previousState);

    int64_t startTime;
    int64_t stopTime;
    int64_t totalElapsedTime = 0;
    int frame = 0;
    uint16_t muteFrameCount = 0;
    bool ignoreMenuButton = previousState.values[ODROID_INPUT_MENU];

    scaling_enabled = odroid_settings_ScaleDisabled_get(ODROID_SCALE_DISABLE_SMS) ? false : true;

    audioBuffer = NULL;
    audioBufferCount = 0;

    /* ---- Main emulation loop ---- */
    while (!smsplus_quit_flag)
    {
        odroid_gamepad_state joystick;
        odroid_input_gamepad_read(&joystick);

        if (ignoreMenuButton)
        {
            ignoreMenuButton = previousState.values[ODROID_INPUT_MENU];
        }

        /* MENU short-press → in-game menu */
        if (!ignoreMenuButton && previousState.values[ODROID_INPUT_MENU] && !joystick.values[ODROID_INPUT_MENU])
        {
            if (!sms_show_ingame_menu()) {
                DoQuit();
                break;
            }
            /* Re-read gamepad after menu dismissal */
            odroid_input_gamepad_read(&joystick);
        }

        /* VOLUME press → volume overlay */
        if (previousState.values[ODROID_INPUT_VOLUME] && !joystick.values[ODROID_INPUT_VOLUME])
        {
            sms_show_volume_overlay();
            /* Re-read gamepad after overlay dismissal */
            odroid_input_gamepad_read(&joystick);
        }

        /* Scaling toggle */
        if (joystick.values[ODROID_INPUT_START] && !previousState.values[ODROID_INPUT_RIGHT] && joystick.values[ODROID_INPUT_RIGHT])
        {
            scaling_enabled = !scaling_enabled;
            odroid_settings_ScaleDisabled_set(ODROID_SCALE_DISABLE_SMS, scaling_enabled ? 0 : 1);
        }

        startTime = esp_timer_get_time();

        /* Map gamepad → SMS input */
        int smsButtons = 0;
        if (joystick.values[ODROID_INPUT_UP])    smsButtons |= INPUT_UP;
        if (joystick.values[ODROID_INPUT_DOWN])  smsButtons |= INPUT_DOWN;
        if (joystick.values[ODROID_INPUT_LEFT])  smsButtons |= INPUT_LEFT;
        if (joystick.values[ODROID_INPUT_RIGHT]) smsButtons |= INPUT_RIGHT;
        if (joystick.values[ODROID_INPUT_A])     smsButtons |= INPUT_BUTTON2;
        if (joystick.values[ODROID_INPUT_B])     smsButtons |= INPUT_BUTTON1;

        int smsSystem = 0;
        if (joystick.values[ODROID_INPUT_START])  smsSystem |= INPUT_START;
        if (joystick.values[ODROID_INPUT_SELECT]) smsSystem |= INPUT_PAUSE;

        input.pad[0] = smsButtons;
        input.system = smsSystem;

        /* Colecovision keypad mapping */
        if (sms.console == CONSOLE_COLECO)
        {
            input.system = 0;
            coleco.keypad[0] = 0xff;
            coleco.keypad[1] = 0xff;

            if (joystick.values[ODROID_INPUT_START])
            {
                coleco.keypad[0] = 1;
            }
            if (previousState.values[ODROID_INPUT_SELECT] && !joystick.values[ODROID_INPUT_SELECT])
            {
                system_reset();
            }
        }

        /* Run one frame — render every frame for smooth scrolling */
        system_frame(0);

        /* Copy to display buffer and send non-blocking (drop if busy) */
        {
            int next = lcdfb_write_idx ^ 1;
            memcpy(lcdfb[next], bitmap.data, 256 * 192);
            lcdfb_write_idx = next;
            uint8_t *arg = lcdfb[next];
            xQueueOverwrite(vidQueue, &arg);
        }
        currentFramebuffer = currentFramebuffer ? 0 : 1;
        bitmap.data = framebuffer[currentFramebuffer];

        /* Audio */
        if (!audioBuffer || audioBufferCount < snd.sample_count)
        {
            if (audioBuffer) free(audioBuffer);

            size_t bufferSize = snd.sample_count * 2 * sizeof(int16_t);
            audioBuffer = heap_caps_malloc(bufferSize, MALLOC_CAP_SPIRAM);
            if (!audioBuffer) abort();

            audioBufferCount = snd.sample_count;
        }

        for (int x = 0; x < snd.sample_count; x++)
        {
            uint32_t sample;
            if (muteFrameCount < 60 * 2)
            {
                sample = 0;
                ++muteFrameCount;
            }
            else
            {
                sample = (snd.output[0][x] << 16) + snd.output[1][x];
            }
            audioBuffer[x] = sample;
        }

        odroid_audio_submit((short *)audioBuffer, snd.sample_count - 1);

        stopTime = esp_timer_get_time();
        previousState = joystick;

        int64_t elapsedTime = stopTime - startTime;
        totalElapsedTime += elapsedTime;
        ++frame;

        if (frame == 60)
        {
            float seconds = totalElapsedTime / 1000000.0f;
            float fps = frame / seconds;

            printf("HEAP:0x%x, FPS:%f, BATTERY:%d [%d]\n",
                   (unsigned)esp_get_free_heap_size(), fps,
                   battery.millivolts, battery.percentage);

            frame = 0;
            totalElapsedTime = 0;
        }
    }

    /* ---- Cleanup ---- */
cleanup:
    system_shutdown();

    if (audioBuffer)
    {
        free(audioBuffer);
        audioBuffer = NULL;
    }

    if (framebuffer[0]) { heap_caps_free(framebuffer[0]); framebuffer[0] = NULL; }
    if (framebuffer[1]) { heap_caps_free(framebuffer[1]); framebuffer[1] = NULL; }

    if (lcdfb[0]) { heap_caps_free(lcdfb[0]); lcdfb[0] = NULL; }
    if (lcdfb[1]) { heap_caps_free(lcdfb[1]); lcdfb[1] = NULL; }

    if (ESP32_PSRAM)
    {
        heap_caps_free(ESP32_PSRAM);
        ESP32_PSRAM = NULL;
    }

    if (vidQueue)
    {
        vQueueDelete(vidQueue);
        vidQueue = NULL;
    }

    smsplus_quit_flag = false;
    printf("smsplus_run: returning to launcher\n");
}
