/*
 * ESP32-P4 PAPP Template — Ball Demo
 *
 * Demonstrates:
 *   - 400×240 RGB565 framebuffer
 *   - PPA rotate 270° + scale 2× → fills 480×800 LCD perfectly
 *   - Gamepad input (D-pad moves ball)
 *   - MENU button to exit back to launcher
 *
 * Build:
 *   .\tools\build_psram_app.ps1 -AppName ESP32_P4_PAPP_Template -Sources ESP32_P4_PAPP_Template\main.c
 *
 * Upload (launcher must be running):
 *   python tools\upload_papp.py firmware\ESP32_P4_PAPP_Template.papp --port COM30
 */

#define PAPP_APP_SIDE 1
#include "psram_app.h"

/* ── Screen dimensions ─────────────────────────────────────────────── */
#define FB_W  400
#define FB_H  240

/* ── RGB565 helpers ────────────────────────────────────────────────── */
#define RGB565(r, g, b) ((((r) & 0x1F) << 11) | (((g) & 0x3F) << 5) | ((b) & 0x1F))

#define COLOR_LIGHT_BLUE  RGB565(17, 51, 29)   /* ~#87CEEB sky blue */
#define COLOR_YELLOW      RGB565(31, 63, 0)    /* #FFFF00 */
#define COLOR_BLACK       0x0000

/* ── Ball parameters ───────────────────────────────────────────────── */
#define BALL_RADIUS  8
#define BALL_SPEED   3

/* ── Globals ───────────────────────────────────────────────────────── */
static uint16_t s_fb[FB_W * FB_H];

/* ── Drawing helpers ───────────────────────────────────────────────── */

static void fb_clear(uint16_t color)
{
    for (int i = 0; i < FB_W * FB_H; i++)
        s_fb[i] = color;
}

static inline void fb_pixel(int x, int y, uint16_t color)
{
    if (x >= 0 && x < FB_W && y >= 0 && y < FB_H)
        s_fb[y * FB_W + x] = color;
}

static void fb_fill_circle(int cx, int cy, int r, uint16_t color)
{
    int r2 = r * r;
    for (int dy = -r; dy <= r; dy++) {
        for (int dx = -r; dx <= r; dx++) {
            if (dx * dx + dy * dy <= r2)
                fb_pixel(cx + dx, cy + dy, color);
        }
    }
}

/* ── Flush framebuffer to LCD via PPA rotate 270° + scale 2× ──────── */

static void fb_flush(const app_services_t *svc)
{
    /*
     * 400×240 → rotate 270° CCW → 240×400, then scale 2× → 480×800.
     * This fills the 480×800 portrait LCD perfectly with no borders.
     */
    svc->display_write_frame_custom(s_fb, FB_W, FB_H, 2.0f, false);
}

/* ── Entry point ───────────────────────────────────────────────────── */

__attribute__((section(".text.entry")))
int app_entry(const app_services_t *svc)
{
    svc->log_printf("=== PAPP Template — Ball Demo ===\n");

    if (svc->abi_version != PAPP_ABI_VERSION) {
        svc->log_printf("ABI mismatch: got %lu, expected %d\n",
                        (unsigned long)svc->abi_version, PAPP_ABI_VERSION);
        return -1;
    }

    /* Ball state — start at center */
    int bx = FB_W / 2;
    int by = FB_H / 2;

    papp_gamepad_state_t pad;

    /* Main loop */
    for (;;) {
        /* ── Input ─────────────────────────────────────────────────── */
        svc->input_gamepad_read(&pad);

        /* MENU button → exit to launcher */
        if (pad.values[PAPP_INPUT_MENU])
            break;

        /* D-pad moves ball */
        if (pad.values[PAPP_INPUT_UP])    by -= BALL_SPEED;
        if (pad.values[PAPP_INPUT_DOWN])  by += BALL_SPEED;
        if (pad.values[PAPP_INPUT_LEFT])  bx -= BALL_SPEED;
        if (pad.values[PAPP_INPUT_RIGHT]) bx += BALL_SPEED;

        /* Clamp to screen bounds */
        if (bx < BALL_RADIUS)            bx = BALL_RADIUS;
        if (bx > FB_W - 1 - BALL_RADIUS) bx = FB_W - 1 - BALL_RADIUS;
        if (by < BALL_RADIUS)            by = BALL_RADIUS;
        if (by > FB_H - 1 - BALL_RADIUS) by = FB_H - 1 - BALL_RADIUS;

        /* ── Draw ──────────────────────────────────────────────────── */
        fb_clear(COLOR_LIGHT_BLUE);
        fb_fill_circle(bx, by, BALL_RADIUS, COLOR_YELLOW);

        /* ── Present ───────────────────────────────────────────────── */
        fb_flush(svc);

        /* ~60 fps target */
        svc->delay_ms(16);
    }

    /* Clean exit */
    svc->display_clear(COLOR_BLACK);
    svc->display_flush();
    svc->log_printf("=== PAPP Template — Exit ===\n");
    return 0;
}
