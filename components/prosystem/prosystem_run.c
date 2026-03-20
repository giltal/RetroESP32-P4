/*
 * prosystem_run.c — Entry point for Atari 7800 (prosystem) emulator on ESP32-P4
 *
 * Adapted from the original prosystem-odroid-go main.c.
 * Loads a ROM, runs the 7800 emulation loop, returns on MENU press.
 */

#include "prosystem_run.h"
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

/* ProSystem core headers */
#include "Bios.h"
#include "Cartridge.h"
#include "Database.h"
#include "Maria.h"
#include "Palette.h"
#include "Pokey.h"
#include "Region.h"
#include "ProSystem.h"
#include "Tia.h"

/* Odroid platform */
#include "odroid_settings.h"
#include "odroid_audio.h"
#include "odroid_input.h"
#include "odroid_system.h"
#include "odroid_display.h"
#include "odroid_sdcard.h"

#define AUDIO_SAMPLE_RATE  32000
#define PRO_FB_WIDTH       320
#define PRO_FB_HEIGHT      240

static const char *SD_BASE_PATH = "/sd";

static QueueHandle_t vidQueue;
static TaskHandle_t videoTaskHandle;

static uint8_t *framebuffer;
static uint16_t *display_palette16;
static uint8_t keyboard_data[17];

/* Double-buffer for non-blocking video (like SMS lcdfb pattern) */
static uint8_t *lcdfb[2] = { NULL, NULL };
static int lcdfb_write_idx = 0;

static int16_t *sampleBuffer;
static size_t sampleBufferLength;

static volatile bool videoTaskIsRunning = false;
static volatile bool prosystem_quit_flag = false;

/* The RenderFlag is referenced by Maria.c (extern bool RenderFlag) */
bool RenderFlag;

/* ─── 5×7 Bitmap Font ──────────────────────────────────────────── */
static const uint8_t pro_font5x7[][7] = {
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

static int pro_font_index(char c)
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
#define PRO_COLOR_BLACK     0x0000
#define PRO_COLOR_WHITE     0xFFFF
#define PRO_COLOR_YELLOW    0xE0FF
#define PRO_COLOR_DKGRAY    0x2108
#define PRO_COLOR_GREEN     0xE007
#define PRO_COLOR_CYAN      0xFF07

static void pro_draw_char(uint16_t *fb, int px, int py, char c, uint16_t color)
{
    int idx = pro_font_index(c);
    const uint8_t *glyph = pro_font5x7[idx];
    for (int row = 0; row < 7; row++) {
        int yy = py + row;
        if (yy < 0 || yy >= PRO_FB_HEIGHT) continue;
        uint8_t bits = glyph[row];
        for (int col = 0; col < 5; col++) {
            int xx = px + col;
            if (xx < 0 || xx >= PRO_FB_WIDTH) continue;
            if (bits & (0x10 >> col)) {
                fb[yy * PRO_FB_WIDTH + xx] = color;
            }
        }
    }
}

static void pro_draw_string(uint16_t *fb, int px, int py, const char *str, uint16_t color)
{
    while (*str) {
        pro_draw_char(fb, px, py, *str, color);
        px += 6;
        str++;
    }
}

static void pro_fill_rect(uint16_t *fb, int x, int y, int w, int h, uint16_t color)
{
    for (int row = y; row < y + h && row < PRO_FB_HEIGHT; row++) {
        if (row < 0) continue;
        for (int col = x; col < x + w && col < PRO_FB_WIDTH; col++) {
            if (col < 0) continue;
            fb[row * PRO_FB_WIDTH + col] = color;
        }
    }
}

/* Forward declarations for save/load */
static void SaveState(void);
static void LoadState(void);

/* ─── Volume Overlay ──────────────────────────────────────────── */
static void pro_show_volume_overlay(void)
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
        odroid_display_lock();

        int box_w = 140, box_h = 34;
        int box_x = (PRO_FB_WIDTH - box_w) / 2, box_y = 8;

        pro_fill_rect(fb, box_x, box_y, box_w, box_h, PRO_COLOR_BLACK);
        pro_fill_rect(fb, box_x, box_y, box_w, 1, PRO_COLOR_WHITE);
        pro_fill_rect(fb, box_x, box_y + box_h - 1, box_w, 1, PRO_COLOR_WHITE);
        pro_fill_rect(fb, box_x, box_y, 1, box_h, PRO_COLOR_WHITE);
        pro_fill_rect(fb, box_x + box_w - 1, box_y, 1, box_h, PRO_COLOR_WHITE);

        char title[32];
        snprintf(title, sizeof(title), "VOLUME: %s", level_names[level]);
        int title_w = strlen(title) * 6;
        pro_draw_string(fb, box_x + (box_w - title_w) / 2, box_y + 4, title, PRO_COLOR_YELLOW);

        int bar_x = box_x + 10, bar_y = box_y + 16, bar_w = box_w - 20, bar_h = 10;
        pro_fill_rect(fb, bar_x, bar_y, bar_w, bar_h, PRO_COLOR_DKGRAY);
        if (level > 0) {
            int fill_w = (bar_w * level) / (ODROID_VOLUME_LEVEL_COUNT - 1);
            uint16_t bar_color = (level <= 1) ? PRO_COLOR_GREEN :
                                 (level <= 3) ? PRO_COLOR_CYAN : PRO_COLOR_YELLOW;
            pro_fill_rect(fb, bar_x, bar_y, fill_w, bar_h, bar_color);
        }
        pro_fill_rect(fb, bar_x, bar_y, bar_w, 1, PRO_COLOR_WHITE);
        pro_fill_rect(fb, bar_x, bar_y + bar_h - 1, bar_w, 1, PRO_COLOR_WHITE);
        pro_fill_rect(fb, bar_x, bar_y, 1, bar_h, PRO_COLOR_WHITE);
        pro_fill_rect(fb, bar_x + bar_w - 1, bar_y, 1, bar_h, PRO_COLOR_WHITE);
        for (int i = 1; i < ODROID_VOLUME_LEVEL_COUNT - 1; i++) {
            int sx = bar_x + (bar_w * i) / (ODROID_VOLUME_LEVEL_COUNT - 1);
            pro_fill_rect(fb, sx, bar_y, 1, bar_h, PRO_COLOR_WHITE);
        }

        display_flush_force();
        odroid_display_unlock();

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

/* ─── Save path helpers ──────────────────────────────────────── */
static char *pro_get_save_path(void)
{
    char *romName = odroid_settings_RomFilePath_get();
    if (!romName) return NULL;

    char *fileName = odroid_util_GetFileName(romName);
    free(romName);
    if (!fileName) return NULL;

    char pathBuf[512];
    snprintf(pathBuf, sizeof(pathBuf), "%s/odroid/data/a78/%s.sav", SD_BASE_PATH, fileName);
    free(fileName);

    return strdup(pathBuf);
}

static bool pro_check_save_exists(void)
{
    char *path = pro_get_save_path();
    if (!path) return false;
    struct stat st;
    bool exists = (stat(path, &st) == 0);
    free(path);
    return exists;
}

static bool pro_delete_save(void)
{
    char *path = pro_get_save_path();
    if (!path) return false;
    struct stat st;
    bool ok = false;
    if (stat(path, &st) == 0) {
        ok = (unlink(path) == 0);
    }
    free(path);
    return ok;
}

/* ─── In-Game Menu ─────────────────────────────────────────────── */
#define PRO_MENU_RESUME     0
#define PRO_MENU_RESTART    1
#define PRO_MENU_SAVE       2
#define PRO_MENU_RELOAD     3
#define PRO_MENU_OVERWRITE  4
#define PRO_MENU_DELETE     5
#define PRO_MENU_EXIT       6

/* Returns: true = keep running, false = exit game */
static bool pro_show_ingame_menu(void)
{
    uint16_t *fb = display_get_framebuffer();
    if (!fb) return true;

    bool has_save = pro_check_save_exists();

    const char *labels[8];
    int ids[8];
    int count = 0;

    labels[count] = "Resume Game";      ids[count] = PRO_MENU_RESUME;    count++;
    labels[count] = "Restart Game";     ids[count] = PRO_MENU_RESTART;   count++;
    labels[count] = "Save Game";        ids[count] = PRO_MENU_SAVE;      count++;
    if (has_save) {
        labels[count] = "Reload Game";  ids[count] = PRO_MENU_RELOAD;    count++;
        labels[count] = "Overwrite Save"; ids[count] = PRO_MENU_OVERWRITE; count++;
        labels[count] = "Delete Save";  ids[count] = PRO_MENU_DELETE;    count++;
    }
    labels[count] = "Exit Game";        ids[count] = PRO_MENU_EXIT;      count++;

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
        odroid_display_lock();

        int box_w = 160;
        int box_h = 20 + count * 14 + 10;
        int box_x = (PRO_FB_WIDTH - box_w) / 2;
        int box_y = (PRO_FB_HEIGHT - box_h) / 2;

        pro_fill_rect(fb, box_x, box_y, box_w, box_h, PRO_COLOR_BLACK);
        pro_fill_rect(fb, box_x, box_y, box_w, 1, PRO_COLOR_WHITE);
        pro_fill_rect(fb, box_x, box_y + box_h - 1, box_w, 1, PRO_COLOR_WHITE);
        pro_fill_rect(fb, box_x, box_y, 1, box_h, PRO_COLOR_WHITE);
        pro_fill_rect(fb, box_x + box_w - 1, box_y, 1, box_h, PRO_COLOR_WHITE);

        pro_draw_string(fb, box_x + (box_w - 9*6)/2, box_y + 5, "GAME MENU", PRO_COLOR_YELLOW);

        for (int i = 0; i < count; i++) {
            int oy = box_y + 18 + i * 14;
            int ox = box_x + 16;
            uint16_t color = (i == selected) ? PRO_COLOR_YELLOW : PRO_COLOR_WHITE;
            pro_fill_rect(fb, box_x + 2, oy - 1, box_w - 4, 10, PRO_COLOR_BLACK);
            if (i == selected) {
                pro_draw_char(fb, box_x + 6, oy, '>', PRO_COLOR_YELLOW);
            }
            pro_draw_string(fb, ox, oy, labels[i], color);
        }

        if (flash_msg && flash_timer > 0) {
            int fw = strlen(flash_msg) * 6;
            int fx = box_x + (box_w - fw) / 2;
            int fy = box_y + box_h - 12;
            pro_fill_rect(fb, box_x + 2, fy - 2, box_w - 4, 12, PRO_COLOR_BLACK);
            pro_draw_string(fb, fx, fy, flash_msg, PRO_COLOR_GREEN);
            flash_timer--;
            if (flash_timer == 0) flash_msg = NULL;
        }

        display_flush_force();
        odroid_display_unlock();

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
                case PRO_MENU_RESUME:
                    menu_active = false;
                    keep_running = true;
                    break;

                case PRO_MENU_RESTART:
                    printf("A7800 InGameMenu: Restart Game\n");
                    prosystem_Reset();
                    menu_active = false;
                    keep_running = true;
                    break;

                case PRO_MENU_RELOAD:
                    printf("A7800 InGameMenu: Reload Game\n");
                    LoadState();
                    flash_msg = "Loaded!";
                    flash_timer = 15;
                    menu_active = false;
                    keep_running = true;
                    break;

                case PRO_MENU_SAVE:
                    printf("A7800 InGameMenu: Save Game\n");
                    SaveState();
                    if (!has_save) {
                        has_save = true;
                        count = 0;
                        labels[count] = "Resume Game";      ids[count] = PRO_MENU_RESUME;    count++;
                        labels[count] = "Restart Game";     ids[count] = PRO_MENU_RESTART;   count++;
                        labels[count] = "Save Game";        ids[count] = PRO_MENU_SAVE;      count++;
                        labels[count] = "Reload Game";      ids[count] = PRO_MENU_RELOAD;    count++;
                        labels[count] = "Overwrite Save";   ids[count] = PRO_MENU_OVERWRITE; count++;
                        labels[count] = "Delete Save";      ids[count] = PRO_MENU_DELETE;    count++;
                        labels[count] = "Exit Game";        ids[count] = PRO_MENU_EXIT;      count++;
                    }
                    flash_msg = "Saved!";
                    flash_timer = 15;
                    break;

                case PRO_MENU_OVERWRITE:
                    printf("A7800 InGameMenu: Overwrite Save\n");
                    SaveState();
                    flash_msg = "Saved!";
                    flash_timer = 15;
                    break;

                case PRO_MENU_DELETE:
                    printf("A7800 InGameMenu: Delete Save\n");
                    if (pro_delete_save()) {
                        has_save = false;
                        flash_msg = "Deleted!";
                        flash_timer = 15;
                        count = 0;
                        labels[count] = "Resume Game";      ids[count] = PRO_MENU_RESUME;    count++;
                        labels[count] = "Restart Game";     ids[count] = PRO_MENU_RESTART;   count++;
                        labels[count] = "Save Game";        ids[count] = PRO_MENU_SAVE;      count++;
                        labels[count] = "Exit Game";        ids[count] = PRO_MENU_EXIT;      count++;
                        if (selected >= count) selected = count - 1;
                    } else {
                        flash_msg = "Error!";
                        flash_timer = 15;
                    }
                    break;

                case PRO_MENU_EXIT:
                    printf("A7800 InGameMenu: Exit Game\n");
                    menu_active = false;
                    keep_running = false;
                    break;
            }
        }

        prev = state;
    }

    return keep_running;
}

/* ─── Video Task ───────────────────────────────────────────────── */
static void videoTask(void *arg)
{
    uint8_t *param;
    videoTaskIsRunning = true;

    while (1) {
        xQueuePeek(vidQueue, &param, portMAX_DELAY);

        if ((uintptr_t)param == 1)
            break;

        ili9341_write_frame_prosystem(param, display_palette16);

        xQueueReceive(vidQueue, &param, portMAX_DELAY);
    }

    odroid_display_lock();
    odroid_display_show_hourglass();
    odroid_display_unlock();

    videoTaskIsRunning = false;
    vTaskDelete(NULL);
    while (1) {}
}

/* ─── Palette Setup ────────────────────────────────────────────── */
static void display_ResetPalette16(void)
{
    display_palette16 = (uint16_t *)malloc(256 * sizeof(uint16_t));
    if (!display_palette16) abort();

    uint8_t *paldata = palette_data;

    for (unsigned index = 0; index < 256; index++) {
        uint16_t r = paldata[(index * 3) + 0];
        uint16_t g = paldata[(index * 3) + 1];
        uint16_t b = paldata[(index * 3) + 2];

        /* RGB888 → RGB565 */
        uint16_t rgb565 = ((r << 8) & 0xf800) | ((g << 3) & 0x07e0) | (b >> 3);
        display_palette16[index] = rgb565;
    }
}

/* ─── Audio Resample ───────────────────────────────────────────── */
static uint8_t tiaSamples[1024];
static uint8_t pokeySamples[1024];

static void sound_Resample(const uint8_t *source, uint8_t *target, int length)
{
    int measurement = AUDIO_SAMPLE_RATE;
    int sourceIndex = 0;
    int targetIndex = 0;
    int max = ((prosystem_frequency * prosystem_scanlines) << 1);

    while (targetIndex < length) {
        if (measurement >= max) {
            target[targetIndex++] = source[sourceIndex];
            measurement -= max;
        } else {
            sourceIndex++;
            measurement += AUDIO_SAMPLE_RATE;
        }
    }
}

/* ─── Save / Load State ───────────────────────────────────────── */
#define SAVE_STATE_SIZE 32829  /* prosystem max state size */

static void SaveState(void)
{
    odroid_input_battery_monitor_enabled_set(0);
    odroid_system_led_set(1);

    char *romName = odroid_settings_RomFilePath_get();
    if (romName) {
        odroid_display_lock();
        odroid_display_drain_spi();

        char *fileName = odroid_util_GetFileName(romName);
        if (!fileName) { free(romName); goto done; }

        /* Create save directory tree: /sd/odroid/data/a78/ */
        char dirBuf[512];
        snprintf(dirBuf, sizeof(dirBuf), "%s/odroid", SD_BASE_PATH);
        mkdir(dirBuf, 0775);
        snprintf(dirBuf, sizeof(dirBuf), "%s/odroid/data", SD_BASE_PATH);
        mkdir(dirBuf, 0775);
        snprintf(dirBuf, sizeof(dirBuf), "%s/odroid/data/a78", SD_BASE_PATH);
        mkdir(dirBuf, 0775);

        char pathBuf[512];
        snprintf(pathBuf, sizeof(pathBuf), "%s/odroid/data/a78/%s.sav", SD_BASE_PATH, fileName);
        printf("A7800 SaveState: saving to '%s'\n", pathBuf);

        /* Allocate state buffer and save */
        char *stateBuffer = (char *)heap_caps_malloc(SAVE_STATE_SIZE, MALLOC_CAP_SPIRAM);
        if (stateBuffer) {
            memset(stateBuffer, 0, SAVE_STATE_SIZE);
            if (prosystem_Save(stateBuffer, false)) {
                FILE *f = fopen(pathBuf, "wb");
                if (f) {
                    fwrite(stateBuffer, 1, SAVE_STATE_SIZE, f);
                    fclose(f);
                    printf("A7800 SaveState: OK.\n");
                } else {
                    printf("A7800 SaveState: fopen failed\n");
                }
            }
            heap_caps_free(stateBuffer);
        }

        odroid_display_unlock();
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
    if (romName) {
        odroid_display_lock();
        odroid_display_drain_spi();

        char *fileName = odroid_util_GetFileName(romName);
        if (!fileName) { free(romName); return; }

        char pathBuf[512];
        snprintf(pathBuf, sizeof(pathBuf), "%s/odroid/data/a78/%s.sav", SD_BASE_PATH, fileName);
        printf("A7800 LoadState: loading from '%s'\n", pathBuf);

        FILE *f = fopen(pathBuf, "rb");
        if (f) {
            char *stateBuffer = (char *)heap_caps_malloc(SAVE_STATE_SIZE, MALLOC_CAP_SPIRAM);
            if (stateBuffer) {
                size_t read = fread(stateBuffer, 1, SAVE_STATE_SIZE, f);
                if (read > 0) {
                    prosystem_Load(stateBuffer);
                    printf("A7800 LoadState: OK (%d bytes).\n", (int)read);
                }
                heap_caps_free(stateBuffer);
            }
            fclose(f);
        } else {
            printf("A7800 LoadState: no save file found.\n");
        }

        odroid_display_unlock();
        free(fileName);
        free(romName);
    }
}

static void DoQuit(void)
{
    uint8_t *param = (uint8_t *)(uintptr_t)1;

    printf("A7800 DoQuit: stopping audio.\n");
    odroid_audio_terminate();

    printf("A7800 DoQuit: stopping video task.\n");
    xQueueSend(vidQueue, &param, portMAX_DELAY);
    while (videoTaskIsRunning) { vTaskDelay(1); }

    printf("A7800 DoQuit: saving state.\n");
    SaveState();

    prosystem_quit_flag = true;
}

/* ─── Emulator Init ────────────────────────────────────────────── */
static void emu_init(const char *filename)
{
    framebuffer = (uint8_t *)heap_caps_malloc(PRO_FB_WIDTH * PRO_FB_HEIGHT, MALLOC_CAP_SPIRAM);
    if (!framebuffer) abort();

    memset(keyboard_data, 0, sizeof(keyboard_data));

    /* Difficulty switches: left=(B)eginner, right=(A)dvanced */
    keyboard_data[15] = 1;  /* Left diff: beginner */
    keyboard_data[16] = 0;  /* Right diff: advanced (fixes Tower Toppler) */

    FILE *fp = fopen(filename, "rb");
    if (!fp) {
        printf("A7800: Failed to open ROM: %s\n", filename);
        return;
    }

    fseek(fp, 0, SEEK_END);
    size_t size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    void *data = heap_caps_malloc(size, MALLOC_CAP_SPIRAM);
    if (!data) abort();

    size_t count = fread(data, 1, size, fp);
    if (count != size) abort();
    fclose(fp);

    if (!cartridge_Load((const uint8_t *)data, size)) {
        printf("A7800: cartridge_Load failed\n");
        heap_caps_free(data);
        return;
    }

    /* Allocate maria_surface in PSRAM (93KB) */
    extern uint8_t *maria_surface;
    if (!maria_surface) {
        maria_surface = heap_caps_malloc(320 * 292, MALLOC_CAP_SPIRAM);
        if (!maria_surface) abort();
        memset(maria_surface, 0, 320 * 292);
    }

    /* BIOS is optional — skip for now */
    database_Load(cartridge_digest);
    prosystem_Reset();
    display_ResetPalette16();
}

/* ─── Main Entry Point ─────────────────────────────────────────── */
void prosystem_run(const char *rom_path)
{
    printf("prosystem_run: starting Atari 7800 emulator, ROM=%s\n", rom_path);

    prosystem_quit_flag = false;

    /* Store ROM path for save/load */
    odroid_settings_RomFilePath_set(rom_path);

    /* Double-buffer for non-blocking video */
    lcdfb[0] = (uint8_t *)heap_caps_malloc(PRO_FB_WIDTH * PRO_FB_HEIGHT, MALLOC_CAP_SPIRAM);
    lcdfb[1] = (uint8_t *)heap_caps_malloc(PRO_FB_WIDTH * PRO_FB_HEIGHT, MALLOC_CAP_SPIRAM);
    if (!lcdfb[0] || !lcdfb[1]) {
        printf("prosystem_run: ERROR — lcdfb alloc failed\n");
        goto cleanup;
    }
    lcdfb_write_idx = 0;

    /* Open SD card and load ROM */
    esp_err_t r = odroid_sdcard_open("/sd");
    if (r != ESP_OK) {
        printf("prosystem_run: ERROR — SD open failed\n");
        goto cleanup;
    }

    emu_init(rom_path);

    /* Audio init */
    odroid_audio_init(AUDIO_SAMPLE_RATE);

    /* Allocate audio sample buffer */
    int audioLength = AUDIO_SAMPLE_RATE / prosystem_frequency;
    sampleBufferLength = audioLength * sizeof(int16_t) * 2;
    sampleBuffer = (int16_t *)malloc(sampleBufferLength);
    if (!sampleBuffer) abort();

    /* Video queue and task */
    vidQueue = xQueueCreate(1, sizeof(uint8_t *));
    xTaskCreatePinnedToCore(&videoTask, "a78_video", 1024 * 4, NULL, 5, &videoTaskHandle, 1);

    /* Check for resume: if StartAction is RESTART, load saved state */
    if (odroid_settings_StartAction_get() == ODROID_START_ACTION_RESTART) {
        odroid_settings_StartAction_set(ODROID_START_ACTION_NORMAL);
        LoadState();
        printf("prosystem_run: resumed from save state\n");
    }

    /* Input state */
    odroid_gamepad_state previousState;
    odroid_input_gamepad_read(&previousState);

    int64_t startTime;
    int64_t stopTime;
    int64_t totalElapsedTime = 0;
    int frame = 0;
    int renderFrames = 0;
    bool ignoreMenuButton = previousState.values[ODROID_INPUT_MENU];

    /* ---- Main emulation loop ---- */
    while (!prosystem_quit_flag) {
        odroid_gamepad_state joystick;
        odroid_input_gamepad_read(&joystick);

        if (ignoreMenuButton) {
            ignoreMenuButton = previousState.values[ODROID_INPUT_MENU];
        }

        /* MENU button → in-game menu */
        if (!ignoreMenuButton && previousState.values[ODROID_INPUT_MENU] &&
            !joystick.values[ODROID_INPUT_MENU]) {
            /* Drain the video queue */
            uint8_t *dummy;
            while (xQueueReceive(vidQueue, &dummy, 0) == pdTRUE) {}
            vTaskDelay(pdMS_TO_TICKS(50));

            if (!pro_show_ingame_menu()) {
                DoQuit();
                break;
            }
            odroid_input_gamepad_read(&joystick);
        }

        /* VOLUME button → volume overlay */
        if (previousState.values[ODROID_INPUT_VOLUME] &&
            !joystick.values[ODROID_INPUT_VOLUME]) {
            pro_show_volume_overlay();
            odroid_input_gamepad_read(&joystick);
        }

        startTime = esp_timer_get_time();

        /* Map gamepad → prosystem keyboard_data */
        keyboard_data[0] = joystick.values[ODROID_INPUT_RIGHT];
        keyboard_data[1] = joystick.values[ODROID_INPUT_LEFT];
        keyboard_data[2] = joystick.values[ODROID_INPUT_DOWN];
        keyboard_data[3] = joystick.values[ODROID_INPUT_UP];
        keyboard_data[4] = joystick.values[ODROID_INPUT_B];
        keyboard_data[5] = joystick.values[ODROID_INPUT_A];
        keyboard_data[13] = joystick.values[ODROID_INPUT_SELECT];
        keyboard_data[14] = joystick.values[ODROID_INPUT_START];

        /* Run one frame */
        RenderFlag = frame & 1;  /* render every other frame */
        prosystem_ExecuteFrame(keyboard_data);

        if (RenderFlag) {
            /* Extract visible 320×240 from maria_surface */
            const uint8_t *buffer = maria_surface + (maria_visibleArea.top * 320);
            int vert = maria_visibleArea.bottom - maria_visibleArea.top;
            int offset = (vert < 271) ? ((240 / 2) - (192 / 2)) : 0;
            buffer -= offset * 320;

            /* Copy to display buffer and send to video task */
            int next = lcdfb_write_idx ^ 1;
            memcpy(lcdfb[next], buffer, PRO_FB_WIDTH * PRO_FB_HEIGHT);
            lcdfb_write_idx = next;
            uint8_t *arg = lcdfb[next];
            xQueueOverwrite(vidQueue, &arg);

            ++renderFrames;
        }

        /* Audio */
        int length = AUDIO_SAMPLE_RATE / prosystem_frequency;

        memset(tiaSamples, 0, sizeof(tiaSamples));
        sound_Resample(tia_buffer, tiaSamples, length);

        if (cartridge_pokey) {
            memset(pokeySamples, 0, sizeof(pokeySamples));
            sound_Resample(pokey_buffer, pokeySamples, length);
        }

        /* Convert 8u to 16s stereo */
        uint32_t *framePtr = (uint32_t *)sampleBuffer;
        for (int i = 0; i < length; i++) {
            int16_t sample16 = (tiaSamples[i] - 128);
            if (cartridge_pokey) {
                sample16 += (pokeySamples[i] - 128);
                sample16 >>= 1;
            }
            sample16 <<= 8;
            framePtr[i] = ((uint32_t)(uint16_t)sample16 << 16) | (uint16_t)sample16;
        }

        odroid_audio_submit((int16_t *)sampleBuffer, length);

        previousState = joystick;

        stopTime = esp_timer_get_time();
        int64_t elapsedTime = stopTime - startTime;
        totalElapsedTime += elapsedTime;
        ++frame;

        if (frame == 60) {
            float seconds = totalElapsedTime / 1000000.0f;
            float fps = frame / seconds;
            float renderFps = renderFrames / seconds;

            printf("A7800 HEAP:0x%x, SIM:%f, REN:%f\n",
                   (unsigned)esp_get_free_heap_size(), fps, renderFps);

            frame = 0;
            renderFrames = 0;
            totalElapsedTime = 0;
        }
    }

    /* ---- Cleanup ---- */
cleanup:
    prosystem_Close();

    if (sampleBuffer) { free(sampleBuffer); sampleBuffer = NULL; }
    if (framebuffer) { heap_caps_free(framebuffer); framebuffer = NULL; }
    if (display_palette16) { free(display_palette16); display_palette16 = NULL; }

    if (lcdfb[0]) { heap_caps_free(lcdfb[0]); lcdfb[0] = NULL; }
    if (lcdfb[1]) { heap_caps_free(lcdfb[1]); lcdfb[1] = NULL; }

    if (vidQueue) {
        vQueueDelete(vidQueue);
        vidQueue = NULL;
    }

    prosystem_quit_flag = false;
    printf("prosystem_run: returning to launcher\n");
}
