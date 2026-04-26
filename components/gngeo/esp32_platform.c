/*
 * esp32_platform.c — ESP32-P4 platform stubs for GnGeo
 *
 * Provides all functions that were originally in:
 *   screen.c, sound.c, event.c, menu.c, conf.c, messages.c, debug.c
 * These are replaced with ESP32 equivalents or no-op stubs.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdbool.h>
#include "SDL.h"
#include "emu.h"
#include "screen.h"
#include "conf.h"
#include "sound.h"
#include "event.h"
#include "menu.h"
#include "messages.h"
#include "video.h"
#include "memory.h"
#include "state.h"
#include "roms.h"
#include "debug.h"
#include "frame_skip.h"

#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "odroid_display.h"
#include "odroid_input.h"
#include "odroid_audio.h"
#include "odroid_settings.h"
#include "st7701_lcd.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"

/* stb_zlib for uncompress() implementation */
#include "stb_zlib.h"

static const char *TAG = "gngeo_esp32";

/* ──────────────────────────────────────────────────────
 * Loading screen — 5×7 bitmap font + progress bar
 * ────────────────────────────────────────────────────── */
static const uint8_t neo_font5x7[][7] = {
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
    /* '.' */ {0x00,0x00,0x00,0x00,0x00,0x00,0x04},
    /* ':' */ {0x00,0x04,0x00,0x00,0x00,0x04,0x00},
    /* '-' */ {0x00,0x00,0x00,0x0E,0x00,0x00,0x00},
    /* '/' */ {0x01,0x02,0x02,0x04,0x08,0x08,0x10},
    /* '%' */ {0x19,0x1A,0x02,0x04,0x08,0x0B,0x13},
    /* '(' */ {0x02,0x04,0x08,0x08,0x08,0x04,0x02},
    /* ')' */ {0x08,0x04,0x02,0x02,0x02,0x04,0x08},
    /* '_' */ {0x00,0x00,0x00,0x00,0x00,0x00,0x1F},
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

static int neo_font_index(char c) {
    if (c >= 'A' && c <= 'Z') return 1 + (c - 'A');
    if (c >= '0' && c <= '9') return 27 + (c - '0');
    if (c == '.') return 37;
    if (c == ':') return 38;
    if (c == '-') return 39;
    if (c == '/') return 40;
    if (c == '%') return 41;
    if (c == '(') return 42;
    if (c == ')') return 43;
    if (c == '_') return 44;
    if (c >= 'a' && c <= 'z') return 45 + (c - 'a');
    return 0; /* space */
}

/* Scale factor for loading screen text (2x = 10x14 per char) */
#define LS_SCALE 2
#define LS_CHAR_W (5 * LS_SCALE)
#define LS_CHAR_H (7 * LS_SCALE)
#define LS_SPACING (6 * LS_SCALE)

/* RGB565 colors */
#define COL_BLACK   0x0000
#define COL_WHITE   0xFFFF
#define COL_YELLOW  0xFFE0
#define COL_CYAN    0x07FF
#define COL_DKGRAY  0x4208
#define COL_GREEN   0x07E0
#define COL_BLUE    0x001F

/* Loading screen state */
static int pbar_total = 1;
static int pbar_pos = 0;
static char pbar_step_msg[64] = "";
static char pbar_game_name[64] = "";

static void ls_draw_char(uint16_t *fb, int px, int py, char c, uint16_t color) {
    int idx = neo_font_index(c);
    const uint8_t *glyph = neo_font5x7[idx];
    for (int row = 0; row < 7; row++) {
        uint8_t bits = glyph[row];
        for (int col = 0; col < 5; col++) {
            if (bits & (0x10 >> col)) {
                for (int sy = 0; sy < LS_SCALE; sy++) {
                    int yy = py + row * LS_SCALE + sy;
                    if (yy < 0 || yy >= 240) continue;
                    for (int sx = 0; sx < LS_SCALE; sx++) {
                        int xx = px + col * LS_SCALE + sx;
                        if (xx < 0 || xx >= 320) continue;
                        fb[yy * 320 + xx] = color;
                    }
                }
            }
        }
    }
}

static void ls_draw_string(uint16_t *fb, int px, int py, const char *str, uint16_t color) {
    while (*str) {
        ls_draw_char(fb, px, py, *str, color);
        px += LS_SPACING;
        str++;
    }
}

static void ls_fill_rect(uint16_t *fb, int x, int y, int w, int h, uint16_t color) {
    for (int row = y; row < y + h && row < 240; row++) {
        if (row < 0) continue;
        for (int col = x; col < x + w && col < 320; col++) {
            if (col < 0) continue;
            fb[row * 320 + col] = color;
        }
    }
}

static void ls_draw_centered(uint16_t *fb, int y, const char *str, uint16_t color) {
    int len = strlen(str);
    int px = (320 - len * LS_SPACING) / 2;
    ls_draw_string(fb, px, y, str, color);
}

/* Loading screen framebuffer — static to avoid repeated alloc */
static uint16_t *ls_fb = NULL;
#define LS_W 320
#define LS_H 240

static void loading_screen_refresh(void) {
    if (!ls_fb) {
        ls_fb = heap_caps_malloc(LS_W * LS_H * sizeof(uint16_t), MALLOC_CAP_SPIRAM);
        if (!ls_fb) return;
    }

    /* Clear screen to dark background */
    ls_fill_rect(ls_fb, 0, 0, LS_W, LS_H, COL_BLACK);

    /* Title: "NEO GEO" at top */
    ls_draw_centered(ls_fb, 30, "NEO GEO", COL_YELLOW);

    /* Game name */
    if (pbar_game_name[0]) {
        ls_draw_centered(ls_fb, 60, pbar_game_name, COL_WHITE);
    }

    /* Current step message */
    if (pbar_step_msg[0]) {
        ls_draw_centered(ls_fb, 100, pbar_step_msg, COL_CYAN);
    }

    /* Progress bar background */
    int bar_x = 40, bar_y = 140, bar_w = 240, bar_h = 16;
    ls_fill_rect(ls_fb, bar_x, bar_y, bar_w, bar_h, COL_DKGRAY);

    /* Progress bar fill (use int64 to avoid overflow — pos can be millions of bytes) */
    int pct = 0;
    if (pbar_total > 0 && pbar_pos > 0) {
        int fill = (int)(((int64_t)pbar_pos * bar_w) / pbar_total);
        if (fill > bar_w) fill = bar_w;
        ls_fill_rect(ls_fb, bar_x, bar_y, fill, bar_h, COL_GREEN);
        pct = (int)(((int64_t)pbar_pos * 100) / pbar_total);
    }

    /* Percentage text */
    if (pct > 100) pct = 100;
    char pct_str[8];
    snprintf(pct_str, sizeof(pct_str), "%d%%", pct);
    ls_draw_centered(ls_fb, bar_y + bar_h + 8, pct_str, COL_WHITE);

    /* "Please wait..." at bottom */
    ls_draw_centered(ls_fb, 200, "Please wait...", COL_DKGRAY);

    /* Use the same display path as the emulator */
    ili9341_write_frame_rgb565_custom(ls_fb, LS_W, LS_H, 2.0f, false);
}

/* ──────────────────────────────────────────────────────
 * Sidebar buttons + in-game menu/volume overlays
 * (same pattern as SNES emulator)
 * ────────────────────────────────────────────────────── */

/* 5×5 bitmap font for menu overlays (A-Z only) */
static const uint8_t menu_font5x5[][5] = {
    /* A */ {0x0E,0x11,0x1F,0x11,0x11},
    /* B */ {0x1E,0x11,0x1E,0x11,0x1E},
    /* C */ {0x0F,0x10,0x10,0x10,0x0F},
    /* D */ {0x1E,0x11,0x11,0x11,0x1E},
    /* E */ {0x1F,0x10,0x1E,0x10,0x1F},
    /* F */ {0x1F,0x10,0x1E,0x10,0x10},
    /* G */ {0x0F,0x10,0x13,0x11,0x0F},
    /* H */ {0x11,0x11,0x1F,0x11,0x11},
    /* I */ {0x0E,0x04,0x04,0x04,0x0E},
    /* J */ {0x01,0x01,0x01,0x11,0x0E},
    /* K */ {0x11,0x12,0x1C,0x12,0x11},
    /* L */ {0x10,0x10,0x10,0x10,0x1F},
    /* M */ {0x11,0x1B,0x15,0x11,0x11},
    /* N */ {0x11,0x19,0x15,0x13,0x11},
    /* O */ {0x0E,0x11,0x11,0x11,0x0E},
    /* P */ {0x1E,0x11,0x1E,0x10,0x10},
    /* Q */ {0x0E,0x11,0x15,0x12,0x0D},
    /* R */ {0x1E,0x11,0x1E,0x12,0x11},
    /* S */ {0x0F,0x10,0x0E,0x01,0x1E},
    /* T */ {0x1F,0x04,0x04,0x04,0x04},
    /* U */ {0x11,0x11,0x11,0x11,0x0E},
    /* V */ {0x11,0x11,0x11,0x0A,0x04},
    /* W */ {0x11,0x11,0x15,0x1B,0x11},
    /* X */ {0x11,0x0A,0x04,0x0A,0x11},
    /* Y */ {0x11,0x0A,0x04,0x04,0x04},
    /* Z */ {0x1F,0x02,0x04,0x08,0x1F},
};

static void menu_draw_char(uint16_t *fb, int fbw, int fbh, int cx, int cy, char ch, uint16_t color)
{
    int idx = -1;
    if (ch >= 'A' && ch <= 'Z') idx = ch - 'A';
    else if (ch >= 'a' && ch <= 'z') idx = ch - 'a';
    if (idx < 0) return;
    for (int row = 0; row < 5; row++)
        for (int col = 0; col < 5; col++)
            if (menu_font5x5[idx][row] & (0x10 >> col))
                if ((cy + row) < fbh && (cx + col) < fbw)
                    fb[(cy + row) * fbw + (cx + col)] = color;
}

static void menu_draw_str(uint16_t *fb, int fbw, int fbh, int x, int y, const char *s, uint16_t color)
{
    while (*s) { menu_draw_char(fb, fbw, fbh, x, y, *s++, color); x += 6; }
}

static void menu_draw_num(uint16_t *fb, int fbw, int fbh, int x, int y, int val, uint16_t color)
{
    char buf[16];
    snprintf(buf, sizeof(buf), "%d", val);
    /* Draw digits as small 3x5 bitmaps */
    static const uint8_t digit_font[][5] = {
        /* 0 */ {0x07,0x05,0x05,0x05,0x07},
        /* 1 */ {0x02,0x06,0x02,0x02,0x07},
        /* 2 */ {0x07,0x01,0x07,0x04,0x07},
        /* 3 */ {0x07,0x01,0x07,0x01,0x07},
        /* 4 */ {0x05,0x05,0x07,0x01,0x01},
        /* 5 */ {0x07,0x04,0x07,0x01,0x07},
        /* 6 */ {0x07,0x04,0x07,0x05,0x07},
        /* 7 */ {0x07,0x01,0x01,0x01,0x01},
        /* 8 */ {0x07,0x05,0x07,0x05,0x07},
        /* 9 */ {0x07,0x05,0x07,0x01,0x07},
    };
    for (const char *p = buf; *p; p++) {
        if (*p >= '0' && *p <= '9') {
            int d = *p - '0';
            for (int row = 0; row < 5; row++)
                for (int col = 0; col < 3; col++)
                    if (digit_font[d][row] & (0x04 >> col))
                        if ((y + row) < fbh && (x + col) < fbw)
                            fb[(y + row) * fbw + (x + col)] = color;
        }
        x += 4;
    }
}

/* Sidebar button buffers (portrait coords, drawn once to DPI FB) */
static uint16_t *s_sidebar_buf[2] = { NULL, NULL };
static const struct { const char *text; int px, py, pw, ph; } neo_sidebar_btns[] = {
    { "MENU", 200,  2,  80, 90 },    /* landscape LEFT sidebar  (portrait top,    game starts y=96) */
    { "VOL",  200, 708, 80, 84 },    /* landscape RIGHT sidebar (portrait bottom, game ends   y=704) */
};
static int sidebar_countdown = 2;  /* blit sidebar for first N frames */

static void neo_init_sidebar_buttons(void)
{
    enum { SC = 3 };
    enum { CW = 5 * SC, CH = 5 * SC, GAP = SC };
    const uint16_t COL_BG  = 0x18E3;
    const uint16_t COL_BRD = 0x6B4D;
    const uint16_t COL_TXT = 0xFFFF;

    for (int b = 0; b < 2; b++) {
        const int pw = neo_sidebar_btns[b].pw, ph = neo_sidebar_btns[b].ph;

        s_sidebar_buf[b] = (uint16_t *)heap_caps_aligned_calloc(
            64, pw * ph, sizeof(uint16_t),
            MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA);
        if (!s_sidebar_buf[b]) { ESP_LOGE(TAG, "Sidebar buf alloc failed b=%d", b); continue; }

        uint16_t *buf = s_sidebar_buf[b];
        for (int i = 0; i < pw * ph; i++) buf[i] = COL_BG;

        /* 2-pixel border */
        for (int t = 0; t < 2; t++) {
            for (int x = 0; x < pw; x++) {
                buf[t * pw + x] = COL_BRD;
                buf[(ph - 1 - t) * pw + x] = COL_BRD;
            }
            for (int y = 0; y < ph; y++) {
                buf[y * pw + t] = COL_BRD;
                buf[y * pw + pw - 1 - t] = COL_BRD;
            }
        }

        /* Render text rotated for landscape reading */
        const char *s = neo_sidebar_btns[b].text;
        int nch = 0;
        for (const char *p = s; *p; p++) nch++;
        int txt_pw = CH;
        int txt_ph = nch * (CW + GAP) - GAP;
        int ox = (pw - txt_pw) / 2;
        int oy = (ph - txt_ph) / 2;
        int glyph_top_x = ox + txt_pw - 1;

        for (int ci = 0; ci < nch; ci++) {
            int idx = -1;
            char ch = s[ci];
            if (ch >= 'A' && ch <= 'Z') idx = ch - 'A';
            else if (ch >= 'a' && ch <= 'z') idx = ch - 'a';
            if (idx < 0) continue;

            int char_by = oy + ci * (CW + GAP);
            for (int fr = 0; fr < 5; fr++)
                for (int fc = 0; fc < 5; fc++)
                    if (menu_font5x5[idx][fr] & (0x10 >> fc))
                        for (int sr = 0; sr < SC; sr++)
                            for (int sc = 0; sc < SC; sc++) {
                                int bx = glyph_top_x - (fr * SC + sr);
                                int by = char_by + fc * SC + sc;
                                if (bx >= 0 && bx < pw && by >= 0 && by < ph)
                                    buf[by * pw + bx] = COL_TXT;
                            }
        }
        ESP_LOGI(TAG, "Sidebar btn[%d] '%s' rendered", b, neo_sidebar_btns[b].text);
    }
}

static void neo_blit_sidebar_buttons(void)
{
    for (int b = 0; b < 2; b++) {
        if (!s_sidebar_buf[b]) continue;
        st7701_lcd_draw_to_fb(
            (uint16_t)neo_sidebar_btns[b].px, (uint16_t)neo_sidebar_btns[b].py,
            (uint16_t)neo_sidebar_btns[b].pw, (uint16_t)neo_sidebar_btns[b].ph,
            s_sidebar_buf[b]);
    }
}

/* Neo Geo visible framebuffer dimensions */
#define NEO_FB_W 304
#define NEO_FB_H 224

/* ── Async double-buffered display ── */
static uint16_t *lcd_fb_arr[2] = { NULL, NULL };
static int lcd_fb_write = 0;
static QueueHandle_t neo_vidQueue = NULL;
static TaskHandle_t neo_videoTaskHandle = NULL;
static volatile bool neo_videoTaskRunning = false;

/* Forward declaration — points to current write buffer */
static uint16_t *lcd_fb;

static void neo_show_volume(void)
{
    static const char * const level_names[] = { "MUTE", "LOW", "MED", "HIGH", "MAX" };

    int level = (int)odroid_audio_volume_get();
    level = (level + 1) % ODROID_VOLUME_LEVEL_COUNT;
    odroid_audio_volume_set(level);
    odroid_settings_Volume_set(level);

    int timeout = 25;

    if (!lcd_fb) return;

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
        int box_w = 140, box_h = 34;
        int box_x = (NEO_FB_W - box_w) / 2;
        int box_y = 4;

        /* Dark background + border */
        for (int r = box_y; r < box_y + box_h && r < NEO_FB_H; r++)
            for (int c = box_x; c < box_x + box_w && c < NEO_FB_W; c++)
                lcd_fb[r * NEO_FB_W + c] = COL_BLACK;
        for (int c = box_x; c < box_x + box_w; c++) {
            if (box_y < NEO_FB_H) lcd_fb[box_y * NEO_FB_W + c] = COL_WHITE;
            if (box_y + box_h - 1 < NEO_FB_H) lcd_fb[(box_y + box_h - 1) * NEO_FB_W + c] = COL_WHITE;
        }
        for (int r = box_y; r < box_y + box_h && r < NEO_FB_H; r++) {
            lcd_fb[r * NEO_FB_W + box_x] = COL_WHITE;
            lcd_fb[r * NEO_FB_W + box_x + box_w - 1] = COL_WHITE;
        }

        /* Title */
        char title[32];
        snprintf(title, sizeof(title), "VOL %s", level_names[level]);
        menu_draw_str(lcd_fb, NEO_FB_W, NEO_FB_H, box_x + 8, box_y + 4, title, COL_YELLOW);

        /* Volume bar */
        int bar_x = box_x + 6;
        int bar_y = box_y + 16;
        int bar_w = box_w - 12;
        int bar_h = 10;
        for (int r = bar_y; r < bar_y + bar_h && r < NEO_FB_H; r++)
            for (int c = bar_x; c < bar_x + bar_w && c < NEO_FB_W; c++)
                lcd_fb[r * NEO_FB_W + c] = COL_DKGRAY;
        if (level > 0) {
            int fill = (bar_w * level) / (ODROID_VOLUME_LEVEL_COUNT - 1);
            uint16_t bar_col = (level <= 1) ? COL_GREEN : (level <= 3) ? COL_CYAN : COL_YELLOW;
            for (int r = bar_y; r < bar_y + bar_h && r < NEO_FB_H; r++)
                for (int c = bar_x; c < bar_x + fill && c < NEO_FB_W; c++)
                    lcd_fb[r * NEO_FB_W + c] = bar_col;
        }

        ili9341_write_frame_rgb565_custom(lcd_fb, NEO_FB_W, NEO_FB_H, 2.0f, false);

        vTaskDelay(pdMS_TO_TICKS(80));
        odroid_gamepad_state state;
        odroid_input_gamepad_read(&state);

        if (state.values[ODROID_INPUT_LEFT] && !prev.values[ODROID_INPUT_LEFT]) {
            if (level > 0) level--;
            odroid_audio_volume_set(level);
            odroid_settings_Volume_set(level);
            timeout = 25;
        }
        if (state.values[ODROID_INPUT_RIGHT] && !prev.values[ODROID_INPUT_RIGHT]) {
            if (level < ODROID_VOLUME_LEVEL_COUNT - 1) level++;
            odroid_audio_volume_set(level);
            odroid_settings_Volume_set(level);
            timeout = 25;
        }
        if (state.values[ODROID_INPUT_VOLUME] && !prev.values[ODROID_INPUT_VOLUME]) {
            level = (level + 1) % ODROID_VOLUME_LEVEL_COUNT;
            odroid_audio_volume_set(level);
            odroid_settings_Volume_set(level);
            timeout = 25;
        }
        if ((state.values[ODROID_INPUT_A] && !prev.values[ODROID_INPUT_A]) ||
            (state.values[ODROID_INPUT_B] && !prev.values[ODROID_INPUT_B])) {
            break;
        }

        prev = state;
        timeout--;
    }
}

static bool neo_show_menu(void)
{
    if (!lcd_fb) return false;

    int sel = 0;
    const int ITEMS = 2;
    const char *labels[] = { "RESUME", "EXIT GAME" };

    odroid_gamepad_state prev;
    odroid_input_gamepad_read(&prev);

    /* Debounce menu button */
    for (int i = 0; i < 50; i++) {
        odroid_gamepad_state tmp;
        odroid_input_gamepad_read(&tmp);
        if (!tmp.values[ODROID_INPUT_MENU]) break;
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    while (1) {
        int box_w = 120, box_h = 18 + ITEMS * 14;
        int box_x = (NEO_FB_W - box_w) / 2;
        int box_y = (NEO_FB_H - box_h) / 2;

        /* Background box */
        for (int r = box_y; r < box_y + box_h && r < NEO_FB_H; r++)
            for (int c = box_x; c < box_x + box_w && c < NEO_FB_W; c++)
                lcd_fb[r * NEO_FB_W + c] = COL_BLACK;

        /* Border */
        for (int c = box_x; c < box_x + box_w; c++) {
            lcd_fb[box_y * NEO_FB_W + c] = COL_WHITE;
            lcd_fb[(box_y + box_h - 1) * NEO_FB_W + c] = COL_WHITE;
        }
        for (int r = box_y; r < box_y + box_h; r++) {
            lcd_fb[r * NEO_FB_W + box_x] = COL_WHITE;
            lcd_fb[r * NEO_FB_W + box_x + box_w - 1] = COL_WHITE;
        }

        /* Menu items */
        for (int i = 0; i < ITEMS; i++) {
            uint16_t color = (i == sel) ? COL_GREEN : COL_WHITE;
            int ty = box_y + 10 + i * 14;
            int tx = box_x + 20;

            /* Selection arrow */
            if (i == sel) {
                for (int dy = 0; dy < 5; dy++)
                    for (int dx = 0; dx < 3; dx++)
                        if ((ty + dy) < NEO_FB_H && (tx - 10 + dx) < NEO_FB_W)
                            lcd_fb[(ty + dy) * NEO_FB_W + tx - 10 + dx] = color;
            }

            menu_draw_str(lcd_fb, NEO_FB_W, NEO_FB_H, tx, ty, labels[i], color);
        }

        ili9341_write_frame_rgb565_custom(lcd_fb, NEO_FB_W, NEO_FB_H, 2.0f, false);

        vTaskDelay(pdMS_TO_TICKS(100));

        odroid_gamepad_state gp;
        odroid_input_gamepad_read(&gp);

        if (gp.values[ODROID_INPUT_UP] && !prev.values[ODROID_INPUT_UP])
            sel = (sel - 1 + ITEMS) % ITEMS;
        if (gp.values[ODROID_INPUT_DOWN] && !prev.values[ODROID_INPUT_DOWN])
            sel = (sel + 1) % ITEMS;
        if (gp.values[ODROID_INPUT_A] && !prev.values[ODROID_INPUT_A]) {
            if (sel == 0) return false;  /* Resume */
            if (sel == 1) return true;   /* Exit */
        }
        if (gp.values[ODROID_INPUT_MENU] && !prev.values[ODROID_INPUT_MENU])
            return false;
        if (gp.values[ODROID_INPUT_B] && !prev.values[ODROID_INPUT_B])
            return false;

        prev = gp;
    }
}

/* ──────────────────────────────────────────────────────
 * Screen globals (declared in screen.h as non-extern!)
 * ────────────────────────────────────────────────────── */
static SDL_PixelFormat screen_fmt = { .BitsPerPixel = 16, .BytesPerPixel = 2,
    .Rmask = 0xF800, .Gmask = 0x07E0, .Bmask = 0x001F, .Amask = 0 };
static SDL_PixelFormat buf_fmt = { .BitsPerPixel = 16, .BytesPerPixel = 2,
    .Rmask = 0xF800, .Gmask = 0x07E0, .Bmask = 0x001F, .Amask = 0 };

/* The real framebuffers — allocated in screen_init */
static SDL_Surface screen_surface;
static SDL_Surface buffer_surface;
static SDL_Surface sprbuf_surface;

/* Global pointers (screen.h now uses extern) */
SDL_Surface *screen = NULL;
SDL_Surface *buffer = NULL;
SDL_Surface *sprbuf = NULL;
SDL_Surface *fps_buf = NULL;
SDL_Surface *scan = NULL;
SDL_Surface *fontbuf = NULL;
SDL_Rect visible_area;

int yscreenpadding = 0;
Uint8 interpolation = 0;
Uint8 nblitter = 0;
Uint8 neffect = 0;
Uint8 scale = 1;
Uint8 fullscreen = 0;

/* ──────────────────────────────────────────────────────
 * Video globals referenced by emu.c / video.c
 * (only define those NOT already in video.c)
 * ────────────────────────────────────────────────────── */

/* Pending save/load state slots */
int pending_save_state = 0;
int pending_load_state = 0;
int show_fps = 0;
int autoframeskip = 0;
int sleep_idle = 0;
int show_keysym = 0;
char input_buf[256];

/* emu.h globals */
Uint8 key[SDLK_LAST];
Uint8 *joy_button[2];
Sint32 *joy_axe[2];
Uint32 joy_numaxes[2];

/* state_img for menu screenshot capture */
static SDL_Surface state_img_surface;
SDL_Surface *state_img = &state_img_surface;
Uint8 state_version = 3;
char fps_str[32];
struct gngeo_conf conf;

/* ──────────────────────────────────────────────────────
 * zlib uncompress via stb_zlib
 * ────────────────────────────────────────────────────── */
int uncompress(uint8_t *dest, unsigned long *destLen,
               const uint8_t *source, unsigned long sourceLen) {
    zbuf z;
    memset(&z, 0, sizeof(z));
    z.zbuffer = (uint8 *)source;
    z.zbuffer_end = (uint8 *)source + sourceLen;
    int result = stbi_zlib_decode_noheader_stream(&z, (char *)dest, (int)*destLen);
    if (result < 0) return -1;
    *destLen = result;
    return 0;
}

/* ──────────────────────────────────────────────────────
 * Screen functions
 * ────────────────────────────────────────────────────── */
#define NEO_SCREEN_W 320
#define NEO_SCREEN_H 224

static uint16_t *screen_pixels;
static uint16_t *buffer_pixels;
static uint16_t *sprbuf_pixels;
/* lcd_fb forward-declared above (used by menu/volume overlays) */

/* ── Neo Geo video task (Core 1) — async PPA + LCD push ── */
static void neo_video_task(void *arg) {
    (void)arg;
    uint16_t *frame = NULL;
    neo_videoTaskRunning = true;

    while (1) {
        xQueuePeek(neo_vidQueue, &frame, portMAX_DELAY);
        if (frame == (uint16_t *)1) break;  /* quit sentinel */

        ili9341_write_frame_rgb565_custom(frame, NEO_FB_W, NEO_FB_H, 2.0f, false);

        /* Blit sidebar button labels after frame push */
        if (sidebar_countdown > 0) {
            neo_blit_sidebar_buttons();
            sidebar_countdown--;
        }

        xQueueReceive(neo_vidQueue, &frame, portMAX_DELAY);
    }

    neo_videoTaskRunning = false;
    vTaskDelete(NULL);
}

int screen_init(void) {
    ESP_LOGI(TAG, "screen_init: %dx%d RGB565", NEO_SCREEN_W + 32, NEO_SCREEN_H + 32);

    /* Allocate in PSRAM for large buffers */
    screen_pixels = heap_caps_calloc(1, (NEO_SCREEN_W + 32) * (NEO_SCREEN_H + 32) * 2, MALLOC_CAP_SPIRAM);
    buffer_pixels = heap_caps_calloc(1, (NEO_SCREEN_W + 32) * (NEO_SCREEN_H + 32) * 2, MALLOC_CAP_SPIRAM);
    sprbuf_pixels = heap_caps_calloc(1, (NEO_SCREEN_W + 32) * (NEO_SCREEN_H + 32) * 2, MALLOC_CAP_SPIRAM);

    if (!screen_pixels || !buffer_pixels || !sprbuf_pixels) {
        ESP_LOGE(TAG, "Failed to allocate screen buffers");
        return -1;
    }

    /* Set up SDL_Surface wrappers */
    memset(&screen_surface, 0, sizeof(screen_surface));
    screen_surface.format = &screen_fmt;
    screen_surface.w = NEO_SCREEN_W + 32;
    screen_surface.h = NEO_SCREEN_H + 32;
    screen_surface.pitch = (NEO_SCREEN_W + 32) * 2;
    screen_surface.pixels = screen_pixels;
    screen = &screen_surface;

    memset(&buffer_surface, 0, sizeof(buffer_surface));
    buffer_surface.format = &buf_fmt;
    buffer_surface.w = NEO_SCREEN_W + 32;
    buffer_surface.h = NEO_SCREEN_H + 32;
    buffer_surface.pitch = (NEO_SCREEN_W + 32) * 2;
    buffer_surface.pixels = buffer_pixels;
    buffer = &buffer_surface;

    memset(&sprbuf_surface, 0, sizeof(sprbuf_surface));
    sprbuf_surface.format = &buf_fmt;
    sprbuf_surface.w = NEO_SCREEN_W + 32;
    sprbuf_surface.h = NEO_SCREEN_H + 32;
    sprbuf_surface.pitch = (NEO_SCREEN_W + 32) * 2;
    sprbuf_surface.pixels = sprbuf_pixels;
    sprbuf = &sprbuf_surface;

    /* Visible area — standard Neo Geo viewport */
    visible_area.x = 16;
    visible_area.y = 16;
    visible_area.w = 304;
    visible_area.h = 224;

    /* fps_buf, scan, fontbuf — not needed, set to NULL */
    fps_buf = NULL;
    scan = NULL;
    fontbuf = NULL;

    /* Allocate double-buffered LCD framebuffer for async display */
    for (int i = 0; i < 2; i++) {
        lcd_fb_arr[i] = heap_caps_calloc(1, 304 * 224 * 2, MALLOC_CAP_SPIRAM);
        if (!lcd_fb_arr[i]) {
            ESP_LOGE(TAG, "Failed to allocate LCD framebuffer %d", i);
            return -1;
        }
    }
    lcd_fb_write = 0;
    lcd_fb = lcd_fb_arr[lcd_fb_write];

    /* Video task is created lazily in screen_update() so it doesn't
     * consume internal DMA RAM before the sprite bounce buffer is
     * allocated during ROM loading. */

    /* Pre-render sidebar button labels (MENU / VOL) */
    neo_init_sidebar_buttons();
    sidebar_countdown = 2;

    return 0;
}

int screen_reinit(void) {
    if (!screen_pixels) return screen_init();
    return 0;
}

int screen_resize(int w, int h) { (void)w; (void)h; return 0; }

void screen_update(void) {
    if (!buffer_pixels || !lcd_fb) return;

    /* Lazy-init video task on first frame (after bounce buffer is allocated) */
    if (!neo_vidQueue) {
        neo_vidQueue = xQueueCreate(1, sizeof(uint16_t *));
        /* Stack in PSRAM to avoid consuming scarce internal DMA RAM */
        static StackType_t *neo_vid_stack;
        static StaticTask_t neo_vid_tcb;
        if (!neo_vid_stack) {
            neo_vid_stack = heap_caps_malloc(4096 * sizeof(StackType_t),
                                            MALLOC_CAP_SPIRAM);
        }
        if (neo_vid_stack) {
            neo_videoTaskHandle = xTaskCreateStaticPinnedToCore(
                neo_video_task, "neo_video", 4096,
                NULL, 5, neo_vid_stack, &neo_vid_tcb, 1);
        }
    }

    /* Extract visible area (304x224) from buffer (352 pixels wide, offset 16,16) */
    const int src_stride = NEO_SCREEN_W + 32; /* 352 */
    const int vis_w = visible_area.w;          /* 304 */
    const int vis_h = visible_area.h;          /* 224 */
    const int ox = visible_area.x;             /* 16  */
    const int oy = visible_area.y;             /* 16  */

    for (int y = 0; y < vis_h; y++) {
        memcpy(&lcd_fb[y * vis_w],
               &buffer_pixels[(oy + y) * src_stride + ox],
               vis_w * sizeof(uint16_t));
    }

    /* Post frame to video task on Core 1 (non-blocking overwrite) */
    if (neo_vidQueue) {
        void *arg = (void *)lcd_fb;
        xQueueOverwrite(neo_vidQueue, &arg);

        /* Flip to other buffer for next frame */
        lcd_fb_write ^= 1;
        lcd_fb = lcd_fb_arr[lcd_fb_write];
    } else {
        /* Fallback: synchronous push */
        ili9341_write_frame_rgb565_custom(lcd_fb, vis_w, vis_h, 2.0f, false);
        if (sidebar_countdown > 0) {
            neo_blit_sidebar_buttons();
            sidebar_countdown--;
        }
    }
}

void screen_close(void) {
    /* Shut down async video task */
    if (neo_vidQueue) {
        uint16_t *sentinel = (uint16_t *)1;
        xQueueOverwrite(neo_vidQueue, &sentinel);
        for (int i = 0; i < 50 && neo_videoTaskRunning; i++)
            vTaskDelay(pdMS_TO_TICKS(20));
        vQueueDelete(neo_vidQueue);
        neo_vidQueue = NULL;
    }
    if (screen_pixels) { heap_caps_free(screen_pixels); screen_pixels = NULL; }
    if (buffer_pixels) { heap_caps_free(buffer_pixels); buffer_pixels = NULL; }
    if (sprbuf_pixels) { heap_caps_free(sprbuf_pixels); sprbuf_pixels = NULL; }
    for (int i = 0; i < 2; i++) {
        if (lcd_fb_arr[i]) { heap_caps_free(lcd_fb_arr[i]); lcd_fb_arr[i] = NULL; }
    }
    lcd_fb = NULL;
}
void screen_fullscreen(void) {}
void init_sdl(void) { screen_init(); }

/* ──────────────────────────────────────────────────────
 * Sound implementation — Z80 + YM2610 on core 1, audio async
 * ────────────────────────────────────────────────────── */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "timer.h"
#include "esp_timer.h"

static SDL_AudioSpec desired_spec, obtain_spec;
SDL_AudioSpec *desired = &desired_spec;
SDL_AudioSpec *obtain = &obtain_spec;

/* Triple-buffered play_buffer: YM2610 renders into one while I2S pushes another */
Uint16 *play_buffer = NULL;          /* points to active render buffer */
static Uint16 *play_buf_a = NULL;
static Uint16 *play_buf_b = NULL;
static Uint16 *play_buf_c = NULL;
static volatile Uint16 *submit_buf = NULL;    /* buffer being submitted by audio task */
static volatile int submit_len = 0;           /* frame count to submit */
static int play_buf_idx = 0;                  /* cycles 0→1→2→0 */

/* Number of stereo samples to generate per frame (sample_rate / 60) */
static int audio_samples_per_frame = 367;

/* ── Z80 task on core 1 ── */
/* Message types for the Z80 command queue */
typedef enum {
    Z80_CMD_NMI,          /* 68K sent sound command → fire NMI + run 300 cycles */
    Z80_CMD_FRAME,        /* Run the per-frame Z80 loop + audio synthesis */
    Z80_CMD_STOP,         /* Shutdown */
} z80_cmd_t;

static QueueHandle_t z80_cmd_queue = NULL;
static SemaphoreHandle_t z80_frame_done_sem = NULL;
static volatile bool z80_task_running = false;

/* Z80 timeslice parameters (set during init) */
static int z80_nb_interlace = 16;
static Uint32 z80_timeslice_interlace = 0;

/* Audio I2S submission synchronization */
static SemaphoreHandle_t audio_ready_sem = NULL;
static SemaphoreHandle_t audio_done_sem = NULL;
static volatile bool audio_task_running = false;

static void audio_submit_task(void *arg) {
    (void)arg;
    while (audio_task_running) {
        if (xSemaphoreTake(audio_ready_sem, pdMS_TO_TICKS(100)) == pdTRUE) {
            volatile Uint16 *buf = submit_buf;
            int len = submit_len;
            if (buf && len > 0) {
                odroid_audio_submit((short *)buf, len);
            }
        }
    }
    vTaskDelete(NULL);
}

static void z80_sound_task(void *arg) {
    (void)arg;
    extern void cpu_z80_run(int nbcycle);
    extern void cpu_z80_nmi(void);
    extern void YM2610Update_stream(int length);

    z80_cmd_t cmd;
    int64_t last_stat = 0;
    int64_t sum_z80 = 0, sum_ym = 0;
    int z80_stat_count = 0;

    while (z80_task_running) {
        if (xQueueReceive(z80_cmd_queue, &cmd, pdMS_TO_TICKS(100)) == pdTRUE) {
            switch (cmd) {
            case Z80_CMD_NMI:
                /* Process sound command from 68K */
                cpu_z80_nmi();
                cpu_z80_run(300);
                break;

            case Z80_CMD_FRAME: {
                /* Drain any pending NMI commands first */
                z80_cmd_t pending;
                while (xQueueReceive(z80_cmd_queue, &pending, 0) == pdTRUE) {
                    if (pending == Z80_CMD_NMI) {
                        cpu_z80_nmi();
                        cpu_z80_run(300);
                    } else if (pending == Z80_CMD_STOP) {
                        z80_task_running = false;
                        goto z80_exit;
                    }
                }

                /* Run Z80 for one frame */
                int64_t tz80_s = esp_timer_get_time();
                for (int i = 0; i < z80_nb_interlace; i++) {
                    cpu_z80_run(z80_timeslice_interlace);
                    my_timer();
                }
                int64_t tz80_e = esp_timer_get_time();

                /* Synthesize audio and hand off to I2S task */
                YM2610Update_stream(audio_samples_per_frame);
                int64_t tym_e = esp_timer_get_time();

                /* Triple-buffer rotation: hand off current buffer, advance to next */
                submit_buf = play_buffer;
                submit_len = audio_samples_per_frame;
                play_buf_idx = (play_buf_idx + 1) % 3;
                static Uint16 *bufs[3];
                bufs[0] = play_buf_a; bufs[1] = play_buf_b; bufs[2] = play_buf_c;
                play_buffer = bufs[play_buf_idx];
                xSemaphoreGive(audio_ready_sem);

                /* Z80 task timing stats */
                sum_z80 += (tz80_e - tz80_s);
                sum_ym += (tym_e - tz80_e);
                z80_stat_count++;
                if (tym_e - last_stat > 1000000) {
                    printf("  Z80task: z80=%lld us, ym=%lld us (n=%d)\n",
                           sum_z80 / z80_stat_count, sum_ym / z80_stat_count, z80_stat_count);
                    sum_z80 = 0; sum_ym = 0; z80_stat_count = 0;
                    last_stat = tym_e;
                }

                /* Signal emu loop that Z80 frame is done */
                xSemaphoreGive(z80_frame_done_sem);
                break;
            }

            case Z80_CMD_STOP:
                z80_task_running = false;
                break;
            }
        }
    }
z80_exit:
    xSemaphoreGive(z80_frame_done_sem); /* unblock any waiter */
    vTaskDelete(NULL);
}

int init_sdl_audio(void) {
    ESP_LOGI(TAG, "init_sdl_audio: initializing ES8311 at %d Hz", conf.sample_rate);

    audio_samples_per_frame = conf.sample_rate / 60;

    /* Allocate triple play_buffers in PSRAM */
    if (!play_buf_a) {
        play_buf_a = heap_caps_calloc(16384, sizeof(Uint16), MALLOC_CAP_SPIRAM);
        play_buf_b = heap_caps_calloc(16384, sizeof(Uint16), MALLOC_CAP_SPIRAM);
        play_buf_c = heap_caps_calloc(16384, sizeof(Uint16), MALLOC_CAP_SPIRAM);
        if (!play_buf_a || !play_buf_b || !play_buf_c) {
            ESP_LOGE(TAG, "Failed to allocate play_buffers in PSRAM");
            return -1;
        }
        play_buffer = play_buf_a;
        play_buf_idx = 0;
    }

    desired->freq = conf.sample_rate;
    desired->format = AUDIO_S16;
    desired->channels = 2;
    desired->samples = audio_samples_per_frame;
    desired->size = audio_samples_per_frame * 2 * sizeof(int16_t);
    desired->callback = NULL;
    desired->userdata = NULL;

    memcpy(obtain, desired, sizeof(SDL_AudioSpec));

    odroid_audio_init(conf.sample_rate);
    /* Set volume to ~50% for comfortable debugging */
    odroid_audio_volume_set(2); /* Level 2 of 4 = 50% */

    /* Compute Z80 timeslice */
    int z80_overclk = CF_VAL(cf_get_item_by_name("z80clock"));
    Uint32 z80_ts = (z80_overclk == 0 ? 73333 : 73333 + (z80_overclk * 73333 / 100));
    z80_timeslice_interlace = z80_ts / (float)z80_nb_interlace;

    /* Create command queue (depth 16: enough for NMI bursts + frame cmd) */
    z80_cmd_queue = xQueueCreate(16, sizeof(z80_cmd_t));
    z80_frame_done_sem = xSemaphoreCreateBinary();

    /* Create audio I2S submission task */
    audio_ready_sem = xSemaphoreCreateBinary();
    audio_task_running = true;

    static StaticTask_t audio_tcb;
    static StackType_t audio_stack[2048];
    xTaskCreateStaticPinnedToCore(audio_submit_task, "neo_audio", 2048,
                                  NULL, 6, audio_stack, &audio_tcb, 1);

    /* Create Z80 sound task on core 1 */
    z80_task_running = true;
    static StaticTask_t z80_tcb;
    static StackType_t z80_stack[4096];
    xTaskCreateStaticPinnedToCore(z80_sound_task, "neo_z80", 4096,
                                  NULL, 7, z80_stack, &z80_tcb, 1);

    ESP_LOGI(TAG, "Audio ready: %d Hz, %d samples/frame, Z80 on core 1",
             conf.sample_rate, audio_samples_per_frame);
    return 0;
}

void close_sdl_audio(void) {
    if (z80_task_running && z80_cmd_queue) {
        z80_cmd_t cmd = Z80_CMD_STOP;
        xQueueSend(z80_cmd_queue, &cmd, pdMS_TO_TICKS(100));
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    z80_task_running = false;
    audio_task_running = false;
    if (audio_ready_sem) xSemaphoreGive(audio_ready_sem);
    vTaskDelay(pdMS_TO_TICKS(150));
    odroid_audio_terminate();
}

void pause_audio(int on) {
    (void)on;
}

/* Called from 68K store handler: queue an NMI for the Z80 task */
void esp32_z80_queue_nmi(void) {
    if (z80_cmd_queue) {
        z80_cmd_t cmd = Z80_CMD_NMI;
        xQueueSend(z80_cmd_queue, &cmd, 0); /* non-blocking */
    }
}

/* Called from emu loop: kick off Z80 frame on core 1 */
void esp32_z80_start_frame(void) {
    if (z80_cmd_queue) {
        z80_cmd_t cmd = Z80_CMD_FRAME;
        xQueueSend(z80_cmd_queue, &cmd, portMAX_DELAY);
    }
}

/* Called from emu loop: wait for Z80 frame to complete */
void esp32_z80_wait_frame(void) {
    if (z80_frame_done_sem) {
        xSemaphoreTake(z80_frame_done_sem, pdMS_TO_TICKS(50));
    }
}

/* Legacy stub — no longer called from emu loop */
void esp32_audio_submit_frame(void) {
    /* Now handled inside z80_sound_task */
}

/* ──────────────────────────────────────────────────────
 * Event / Input
 * ────────────────────────────────────────────────────── */
int init_event(void) { return 0; }
void reset_event(void) {}
int wait_event(void) { return 0; }

/* Event globals */
JOYMAP *jmap = NULL;
Uint8 joy_state[2][GN_MAX_KEY];
static int menu_quit_requested = 0;
static odroid_gamepad_state gp_prev = {0};

int handle_event(void) {
    /* Read gamepad once per frame */
    odroid_gamepad_state gp;
    odroid_input_gamepad_read(&gp);

    /* Map to joy_state for P1 */
    joy_state[0][GN_UP]          = gp.values[ODROID_INPUT_UP];
    joy_state[0][GN_DOWN]        = gp.values[ODROID_INPUT_DOWN];
    joy_state[0][GN_LEFT]        = gp.values[ODROID_INPUT_LEFT];
    joy_state[0][GN_RIGHT]       = gp.values[ODROID_INPUT_RIGHT];
    joy_state[0][GN_A]           = gp.values[ODROID_INPUT_A];
    joy_state[0][GN_B]           = gp.values[ODROID_INPUT_B];
    joy_state[0][GN_C]           = gp.values[ODROID_INPUT_X];
    joy_state[0][GN_D]           = gp.values[ODROID_INPUT_Y];
    joy_state[0][GN_START]       = gp.values[ODROID_INPUT_START];
    joy_state[0][GN_SELECT_COIN] = gp.values[ODROID_INPUT_SELECT];

    /* VOLUME: touch right shoulder or mapped button */
    if (gp.values[ODROID_INPUT_VOLUME] && !gp_prev.values[ODROID_INPUT_VOLUME]) {
        neo_show_volume();
        sidebar_countdown = 2;  /* re-draw sidebar labels after overlay */
    }

    /* MENU: touch left shoulder */
    if (gp.values[ODROID_INPUT_MENU] && !gp_prev.values[ODROID_INPUT_MENU]) {
        if (neo_show_menu()) {
            menu_quit_requested = 1;
            gp_prev = gp;
            return 1;  /* non-zero = open menu → quit */
        }
        sidebar_countdown = 2;  /* re-draw sidebar labels after overlay */
    }

    gp_prev = gp;
    return 0;
}

/*
 * Neo Geo controller registers (active-low: 0 = pressed, 1 = released)
 * intern_p1/p2:  bit0=UP, 1=DOWN, 2=LEFT, 3=RIGHT, 4=A, 5=B, 6=C, 7=D
 * intern_start:  bit0=P1 START, bit2=P2 START, bit3=P1 SELECT, upper=0x8F idle
 * intern_coin:   bit0=COIN1, bit1=COIN2, bit2=SERVICE, 0x07 idle
 */
void update_p1_key(void) {
    Uint8 val = 0xFF;
    if (joy_state[0][GN_UP])    val &= ~(1 << 0);
    if (joy_state[0][GN_DOWN])  val &= ~(1 << 1);
    if (joy_state[0][GN_LEFT])  val &= ~(1 << 2);
    if (joy_state[0][GN_RIGHT]) val &= ~(1 << 3);
    if (joy_state[0][GN_A])     val &= ~(1 << 4);
    if (joy_state[0][GN_B])     val &= ~(1 << 5);
    if (joy_state[0][GN_C])     val &= ~(1 << 6);
    if (joy_state[0][GN_D])     val &= ~(1 << 7);
    memory.intern_p1 = val;
}

void update_p2_key(void) {
    memory.intern_p2 = 0xFF;
}

void update_start(void) {
    Uint8 val = 0xFF;  /* All idle: bits 0-3=buttons, bit4-5=memcard prot, bit6=no card, bit7=1 */
    if (joy_state[0][GN_START])       val &= ~(1 << 0); /* P1 START */
    memory.intern_start = val;
}

void update_coin(void) {
    int coin_pressed = joy_state[0][GN_SELECT_COIN];
    Uint8 val = 0x07;
    if (coin_pressed) val &= ~(1 << 0); /* COIN 1 */
    memory.intern_coin = val;
}

/* ──────────────────────────────────────────────────────
 * Menu stubs
 * ────────────────────────────────────────────────────── */

Uint32 run_menu(void) {
    if (menu_quit_requested) {
        menu_quit_requested = 0;
        return 2; /* 2 = quit */
    }
    return 0; /* 0 = resume */
}
int gn_init_skin(void) { return 0; }
void gn_reset_pbar(void) {}
void gn_init_pbar(char *name, int size) {
    ESP_LOGI(TAG, "Loading: %s (size=%d)", name ? name : "?", size);
    pbar_total = size > 0 ? size : 1;
    pbar_pos = 0;
    loading_screen_refresh();
}
void gn_update_pbar(int pos) {
    /* Throttle screen updates — only refresh every ~5% change */
    int old_pct = (pbar_total > 0) ? (pbar_pos * 20 / pbar_total) : 0;
    pbar_pos = pos;
    int new_pct = (pbar_total > 0) ? (pbar_pos * 20 / pbar_total) : 0;
    if (new_pct != old_pct) {
        loading_screen_refresh();
    }
}
void gn_terminate_pbar(void) {
    pbar_pos = pbar_total;
    loading_screen_refresh();
    ESP_LOGI(TAG, "Loading complete");
    /* Free loading screen buffer — no longer needed once game starts */
    if (ls_fb) {
        free(ls_fb);
        ls_fb = NULL;
    }
}
void gn_loading_info(const char *msg) {
    if (msg) {
        strncpy(pbar_step_msg, msg, sizeof(pbar_step_msg) - 1);
        pbar_step_msg[sizeof(pbar_step_msg) - 1] = '\0';
    }
    loading_screen_refresh();
}
void gn_set_loading_game(const char *name) {
    if (name) {
        strncpy(pbar_game_name, name, sizeof(pbar_game_name) - 1);
        pbar_game_name[sizeof(pbar_game_name) - 1] = '\0';
    }
}
void gn_popup_error(char *name, char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    printf("[ERROR] %s: ", name ? name : "");
    vprintf(fmt, ap);
    printf("\n");
    va_end(ap);
}
int gn_popup_question(char *name, char *fmt, ...) {
    (void)name; (void)fmt;
    return 0;
}

/* ──────────────────────────────────────────────────────
 * Conf (configuration) stubs
 * ────────────────────────────────────────────────────── */

/* Static config items used by the emulator */
static CONF_ITEM cf_rompath   = { .name = "rompath",   .type = CFT_STRING, .data.dt_str = { .str = "/sd/roms/neogeo" } };
static CONF_ITEM cf_biospath  = { .name = "biospath",  .type = CFT_STRING, .data.dt_str = { .str = "/sd/roms/neogeo" } };
static CONF_ITEM cf_blitter   = { .name = "blitter",   .type = CFT_STRING, .data.dt_str = { .str = "soft" } };
static CONF_ITEM cf_effect    = { .name = "effect",    .type = CFT_STRING, .data.dt_str = { .str = "none" } };
static CONF_ITEM cf_transpack = { .name = "transpack", .type = CFT_STRING, .data.dt_str = { .str = "" } };
static CONF_ITEM cf_system    = { .name = "system",    .type = CFT_STRING, .data.dt_str = { .str = "arcade" } };
static CONF_ITEM cf_country   = { .name = "country",   .type = CFT_STRING, .data.dt_str = { .str = "usa" } };
static CONF_ITEM cf_68kclock  = { .name = "68kclock",  .type = CFT_INT,    .data.dt_int = { .val = 0 } };
static CONF_ITEM cf_z80clock  = { .name = "z80clock",  .type = CFT_INT,    .data.dt_int = { .val = 0 } };
static CONF_ITEM cf_debug     = { .name = "debug",     .type = CFT_BOOLEAN,.data.dt_bool = { .boolean = 0 } };
static CONF_ITEM cf_raster    = { .name = "raster",    .type = CFT_BOOLEAN,.data.dt_bool = { .boolean = 0 } };
static CONF_ITEM cf_sound     = { .name = "sound",     .type = CFT_BOOLEAN,.data.dt_bool = { .boolean = 1 } };
static CONF_ITEM cf_interp    = { .name = "interpolation", .type = CFT_INT,.data.dt_int = { .val = 0 } };
static CONF_ITEM cf_scale     = { .name = "scale",     .type = CFT_INT,    .data.dt_int = { .val = 1 } };
static CONF_ITEM cf_fullscreen = { .name = "fullscreen", .type = CFT_BOOLEAN, .data.dt_bool = { .boolean = 0 } };
static CONF_ITEM cf_autoframeskip = { .name = "autoframeskip", .type = CFT_BOOLEAN, .data.dt_bool = { .boolean = 1 } };
static CONF_ITEM cf_showfps   = { .name = "showfps",   .type = CFT_BOOLEAN,.data.dt_bool = { .boolean = 0 } };
static CONF_ITEM cf_sleepidle = { .name = "sleepidle", .type = CFT_BOOLEAN,.data.dt_bool = { .boolean = 0 } };
static CONF_ITEM cf_screen320 = { .name = "screen320", .type = CFT_BOOLEAN,.data.dt_bool = { .boolean = 0 } };
static CONF_ITEM cf_pal       = { .name = "pal",       .type = CFT_BOOLEAN,.data.dt_bool = { .boolean = 0 } };
static CONF_ITEM cf_sample_rate = { .name = "samplerate", .type = CFT_INT, .data.dt_int = { .val = 22050 } };
static CONF_ITEM cf_libglpath = { .name = "libglpath", .type = CFT_STRING, .data.dt_str = { .str = "" } };
static CONF_ITEM cf_datafile  = { .name = "datafile",  .type = CFT_STRING, .data.dt_str = { .str = "/sd/roms/neogeo/gngeo_data.zip" } };
static CONF_ITEM cf_bench     = { .name = "bench",     .type = CFT_BOOLEAN,.data.dt_bool = { .boolean = 0 } };
static CONF_ITEM cf_dump      = { .name = "dump",      .type = CFT_BOOLEAN,.data.dt_bool = { .boolean = 0 } };

/* Config item table */
static CONF_ITEM *cf_items[] = {
    &cf_rompath, &cf_biospath, &cf_blitter, &cf_effect, &cf_transpack,
    &cf_system, &cf_country, &cf_68kclock, &cf_z80clock, &cf_debug,
    &cf_raster, &cf_sound, &cf_interp, &cf_scale, &cf_fullscreen,
    &cf_autoframeskip, &cf_showfps, &cf_sleepidle, &cf_screen320,
    &cf_pal, &cf_sample_rate, &cf_libglpath, &cf_datafile,
    &cf_bench, &cf_dump,
    NULL
};

CONF_ITEM *cf_get_item_by_name(const char *name) {
    for (int i = 0; cf_items[i]; i++) {
        if (strcmp(cf_items[i]->name, name) == 0)
            return cf_items[i];
    }
    /* Return a dummy item to avoid NULL crashes */
    static CONF_ITEM cf_dummy = { .name = "dummy", .type = CFT_INT, .data.dt_int = { .val = 0 } };
    ESP_LOGW(TAG, "cf_get_item_by_name: '%s' not found", name);
    return &cf_dummy;
}

void cf_init(void) {}
void cf_reset_to_default(void) {}
int cf_open_file(char *filename) { (void)filename; return 0; }
int cf_save_file(char *filename, int flags) { (void)filename; (void)flags; return 0; }
int cf_save_option(char *filename, char *optname, int flags) { (void)filename; (void)optname; (void)flags; return 0; }

void cf_create_bool_item(const char *name, const char *help, char so, int def) { (void)name; (void)help; (void)so; (void)def; }
void cf_create_action_item(const char *name, const char *help, char so, int (*action)(struct CONF_ITEM *self)) { (void)name; (void)help; (void)so; (void)action; }
void cf_create_action_arg_item(const char *name, const char *help, const char *ha, char so, int (*action)(struct CONF_ITEM *self)) { (void)name; (void)help; (void)ha; (void)so; (void)action; }
void cf_create_string_item(const char *name, const char *help, const char *ha, char so, const char *def) { (void)name; (void)help; (void)ha; (void)so; (void)def; }
void cf_create_int_item(const char *name, const char *help, const char *ha, char so, int def) { (void)name; (void)help; (void)ha; (void)so; (void)def; }
void cf_create_array_item(const char *name, const char *help, const char *ha, char so, int size, int *def) { (void)name; (void)help; (void)ha; (void)so; (void)size; (void)def; }
void cf_create_str_array_item(const char *name, const char *help, const char *ha, char so, char *def) { (void)name; (void)help; (void)ha; (void)so; (void)def; }
void cf_init_cmd_line(void) {}
int cf_get_non_opt_index(int argc, char *argv[]) { (void)argc; (void)argv; return 0; }

/* ──────────────────────────────────────────────────────
 * Messages stubs
 * ────────────────────────────────────────────────────── */
void draw_message(const char *string) {
    ESP_LOGI(TAG, "MSG: %s", string ? string : "");
}
void stop_message(int param) { (void)param; }
void SDL_textout(SDL_Surface *dest, int x, int y, const char *string) {
    (void)dest; (void)x; (void)y; (void)string;
}
void text_input(const char *message, int x, int y, char *string, int size) {
    (void)message; (void)x; (void)y;
    if (string && size > 0) string[0] = '\0';
}
void error_box(char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
}

/* ──────────────────────────────────────────────────────
 * Debug stubs
 * ────────────────────────────────────────────────────── */
int dbg_step = 0;
void show_bt(void) {}
void add_bt(Uint32 pc) { (void)pc; }
int check_bp(int pc) { (void)pc; return 0; }
void add_bp(int pc) { (void)pc; }
void del_bp(int pc) { (void)pc; }
int dbg_68k_run(Uint32 nbcycle) { (void)nbcycle; return 0; }

/* ──────────────────────────────────────────────────────
 * RGB2YUV table (referenced by screen.h) — allocated in PSRAM to save internal RAM
 * ────────────────────────────────────────────────────── */
RGB2YUV *rgb2yuv = NULL;
void init_rgb2yuv_table(void) {
    if (!rgb2yuv) {
        rgb2yuv = heap_caps_calloc(65536, sizeof(RGB2YUV), MALLOC_CAP_SPIRAM);
    }
}

/* ──────────────────────────────────────────────────────
 * ESP32-specific: set ROM path for conf system
 * ────────────────────────────────────────────────────── */
void esp32_set_rompath(const char *path) {
    strncpy(cf_rompath.data.dt_str.str, path, CF_MAXSTRLEN - 1);
    strncpy(cf_biospath.data.dt_str.str, path, CF_MAXSTRLEN - 1);
}

void esp32_enable_sound(int enable) {
    cf_sound.data.dt_bool.boolean = enable;
    conf.sound = enable;
}

void esp32_init_conf(const char *game_name) {
    memset(&conf, 0, sizeof(conf));
    conf.game = (char *)game_name;
    conf.x_start = 16;
    conf.y_start = 16;
    conf.res_x = 304;
    conf.res_y = 224;
    conf.sample_rate = 11025;
    conf.sound = 1;
    conf.vsync = 0;
    conf.raster = 0;     /* Non-raster mode (simpler) */
    conf.debug = 0;
    conf.system = SYS_ARCADE;
    conf.country = CTY_USA;
    conf.autoframeskip = 1;
    conf.show_fps = 0;
    conf.sleep_idle = 0;
    conf.screen320 = 0;
    conf.pal = 0;
}
