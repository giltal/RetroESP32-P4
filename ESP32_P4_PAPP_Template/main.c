/*
 * ESP32-P4 PAPP Template — Sprite Demo (Single-Buffer + AXI-GDMA)
 *
 * Pipeline per frame:
 *   1. AXI-GDMA copy bg → fb  (2.5 ms)
 *   2. PPA Blend sprite → fb  (0.5 ms)
 *   3. PPA SRM rot+2× → LCD   (7.9 ms)
 *   Total ≈ 11 ms → ~91 FPS
 */

#define PAPP_APP_SIDE 1
#include "psram_app.h"

/* ── Minimal C runtime for -nostdlib build ─────────────────────────── */
void *memset(void *s, int c, unsigned int n)
{
    unsigned char *p = (unsigned char *)s;
    while (n--) *p++ = (unsigned char)c;
    return s;
}

/* ── Screen dimensions ─────────────────────────────────────────────── */
#define FB_W  400
#define FB_H  240
#define FB_BYTES (FB_W * FB_H * sizeof(uint16_t))

#define COLOR_BLACK  0x0000
#define SHIP_SPEED   3

/* ── Entry point ───────────────────────────────────────────────────── */

__attribute__((section(".text.entry")))
int app_entry(const app_services_t *svc)
{
    svc->log_printf("=== PAPP Sprite — Single-Buffer (AXI-GDMA) ===\n");

    if (svc->abi_version != PAPP_ABI_VERSION) {
        svc->log_printf("ABI mismatch: got %lu, expected %d\n",
                        (unsigned long)svc->abi_version, PAPP_ABI_VERSION);
        return -1;
    }

    /* Allocate single DMA-capable framebuffer */
    const unsigned long fb_alloc = PAPP_MEM_CAP_SPIRAM | PAPP_MEM_CAP_DMA;
    uint16_t *fb = (uint16_t *)svc->mem_caps_alloc(FB_BYTES, fb_alloc);
    if (!fb) {
        svc->log_printf("ERROR: Failed to allocate framebuffer\n");
        return -1;
    }

    /* Load PNGs */
    uint16_t bg_w = 0, bg_h = 0;
    uint16_t *bg = svc->png_load_rgb565("/sd/roms/papp/background.png", &bg_w, &bg_h);
    if (bg) svc->log_printf("Loaded background: %ux%u\n", bg_w, bg_h);
    else    svc->log_printf("WARN: background.png not found\n");

    uint16_t sp_w = 0, sp_h = 0;
    uint16_t *ship = svc->png_load_rgb565("/sd/roms/papp/spaceship.png", &sp_w, &sp_h);
    if (!ship) {
        svc->log_printf("ERROR: spaceship.png not found\n");
        if (bg) svc->mem_free(bg);
        svc->mem_free(fb);
        return -1;
    }
    svc->log_printf("Loaded spaceship: %ux%u\n", sp_w, sp_h);

    int sx = (FB_W - sp_w) / 2;
    int sy = (FB_H - sp_h) / 2;

    papp_gamepad_state_t pad;

    for (;;) {
        /* ── Input ─────────────────────────────────────────────────── */
        svc->input_gamepad_read(&pad);
        if (pad.values[PAPP_INPUT_MENU]) break;

        if (pad.values[PAPP_INPUT_UP])    sy -= SHIP_SPEED;
        if (pad.values[PAPP_INPUT_DOWN])  sy += SHIP_SPEED;
        if (pad.values[PAPP_INPUT_LEFT])  sx -= SHIP_SPEED;
        if (pad.values[PAPP_INPUT_RIGHT]) sx += SHIP_SPEED;
        if (sx < 0)           sx = 0;
        if (sy < 0)           sy = 0;
        if (sx + sp_w > FB_W) sx = FB_W - sp_w;
        if (sy + sp_h > FB_H) sy = FB_H - sp_h;

        /* ── 1. AXI-GDMA copy background ──────────────────────────── */
        if (bg) svc->fb_copy(bg, fb, FB_W, FB_H);
        else    memset(fb, 0, FB_BYTES);

        /* ── 2. PPA Blend sprite ───────────────────────────────────── */
        svc->sprite_blit(fb, FB_W, FB_H, sx, sy,
                          ship, sp_w, sp_h, COLOR_BLACK);

        /* ── 3. PPA SRM flush (rot+2×) ─────────────────────────────── */
        svc->display_write_frame_custom(fb, FB_W, FB_H, 2.0f, false);
    }

    if (bg) svc->mem_free(bg);
    svc->mem_free(ship);
    svc->mem_free(fb);

    svc->display_clear(COLOR_BLACK);
    svc->display_flush();
    svc->log_printf("=== PAPP Sprite Demo — Exit ===\n");
    return 0;
}
