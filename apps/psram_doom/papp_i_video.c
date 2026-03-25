/*
 * Doom I_Video shim for PSRAM App — routes through app_services_t.
 *
 * Replaces the retro-go display calls from prboom-go's main.c with
 * service table calls that go through the PPA engine for scale+rotate.
 *
 * The Doom engine renders into screens[0].data as 8-bit indexed pixels.
 * I_FinishUpdate converts that to RGB565 using the current palette LUT,
 * then submits via display_write_frame_custom (320×200, scale 2.4).
 */
#include "psram_app.h"
#include <string.h>
#include <stdint.h>

/* From prboom engine */
#include <doomdef.h>
#include <v_video.h>
#include <st_stuff.h>
#include <r_fps.h>

extern const app_services_t *_papp_svc;
extern volatile int papp_exit_requested;

extern int SCREENWIDTH;
extern int SCREENHEIGHT;

/* Palette LUT: index → RGB565 */
static uint16_t palette_lut[256];
int current_palette = 0;

/* Frame conversion buffer (allocated in PSRAM) */
static uint16_t *s_conv_buf = NULL;

/* Surface structure for retro-go compatibility in stubs */
static uint16_t surface_palette[256];
static uint8_t *surface_data = NULL;

/* ── rg_surface_t used by retro-go stubs ─────────────────────────────── */
#include <rg_system.h>
static rg_surface_t s_surface;

rg_surface_t *papp_get_surface(void)
{
    return &s_surface;
}

/* ── I_Video interface functions ─────────────────────────────────────── */

void I_StartFrame(void)
{
    /* nothing */
}

void I_UpdateNoBlit(void)
{
    /* nothing */
}

void I_FinishUpdate(void)
{
    if (!surface_data || !s_conv_buf)
        return;

    /* Convert 8bpp indexed → RGB565 */
    const int w = SCREENWIDTH;
    const int h = SCREENHEIGHT;

    for (int y = 0; y < h; y++) {
        uint16_t *dst_row = s_conv_buf + y * 320;
        const uint8_t *src_row = surface_data + y * w;
        for (int x = 0; x < w; x++) {
            dst_row[x] = palette_lut[src_row[x]];
        }
        if (w < 320)
            __builtin_memset(dst_row + w, 0, (320 - w) * sizeof(uint16_t));
    }
    if (h < 240)
        __builtin_memset(s_conv_buf + h * 320, 0, (240 - h) * 320 * sizeof(uint16_t));

    /* Submit: 320×200 input, scale 2.4, rotate 270° via PPA */
    _papp_svc->display_write_frame_custom(s_conv_buf, 320, h, 2.4f, false);
}

bool I_StartDisplay(void)
{
    return true;
}

void I_EndDisplay(void)
{
    /* nothing */
}

void I_SetPalette(int pal)
{
    /* V_BuildPalette returns a malloc'd uint16_t[256] array of RGB565 values */
    uint16_t *palette = V_BuildPalette(pal, 16);
    for (int i = 0; i < 256; i++) {
        palette_lut[i] = palette[i];
        surface_palette[i] = palette[i];
    }
    Z_Free(palette);
    current_palette = pal;
}

void I_InitGraphics(void)
{
    /* Allocate framebuffer from PSRAM */
    surface_data = (uint8_t *)_papp_svc->mem_caps_alloc(
        SCREENWIDTH * SCREENHEIGHT, PAPP_MEM_CAP_SPIRAM);
    if (!surface_data) {
        _papp_svc->log_printf("DOOM: Failed to allocate framebuffer\n");
        return;
    }
    __builtin_memset(surface_data, 0, SCREENWIDTH * SCREENHEIGHT);

    /* Allocate RGB565 conversion buffer */
    s_conv_buf = (uint16_t *)_papp_svc->mem_caps_alloc(
        320 * 240 * sizeof(uint16_t), PAPP_MEM_CAP_SPIRAM);
    if (!s_conv_buf) {
        _papp_svc->log_printf("DOOM: Failed to allocate conv buffer\n");
        return;
    }

    /* Set up rg_surface (used by stubs) */
    s_surface.width = SCREENWIDTH;
    s_surface.height = SCREENHEIGHT;
    s_surface.stride = SCREENWIDTH;
    s_surface.format = 0x81; /* RG_PIXEL_PAL565_BE */
    s_surface.palette = surface_palette;
    s_surface.data = surface_data;

    /* Set up Doom screens[] array */
    for (int i = 0; i < 3; i++) {
        screens[i].width = SCREENWIDTH;
        screens[i].height = SCREENHEIGHT;
        screens[i].byte_pitch = SCREENWIDTH;
    }

    /* Main screen uses our allocated buffer */
    screens[0].data = surface_data;
    screens[0].not_on_heap = true;

    /* Status bar */
    screens[4].width = SCREENWIDTH;
    screens[4].height = (ST_SCALED_HEIGHT + 1);
    screens[4].byte_pitch = SCREENWIDTH;
}

void I_UpdateVideoMode(void)
{
    /* nothing — resolution is fixed */
}

void I_ShutdownGraphics(void)
{
    /* nothing — memory freed by loader */
}
