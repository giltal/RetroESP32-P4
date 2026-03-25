/*
 * Quake VID shim for PSRAM App — routes through app_services_t.
 *
 * Replaces vid_esp32.c: allocates buffers via service table, converts
 * 8-bit indexed palette to RGB565, submits via display_write_frame_custom.
 *
 * 320×240 render, 8-bit indexed pixels with palette conversion.
 */
#include "psram_app.h"
#include <string.h>
#include <stdint.h>
#include <math.h>

#include "quakedef.h"
#include "d_local.h"

extern const app_services_t *_papp_svc;
extern volatile int papp_exit_requested;

#define BASEWIDTH  320
#define BASEHEIGHT 240

/* Palette LUT: index → RGB565 (big-endian swapped for PPA) */
static uint16_t palette_lut[256];
static uint8_t current_palette[256 * 3];
static bool palette_dirty = true;

/* Frame conversion buffer (8bpp → RGB565, allocated in PSRAM) */
static uint16_t *s_conv_buf = NULL;

/* Z-buffer and surfcache (allocated in PSRAM, replaces static BSS arrays) */
static int16_t *s_zbuffer = NULL;
#define SURFCACHE_SIZE (640 * 1024)
static uint8_t *s_surfcache = NULL;

/* Double-buffered surface data */
static uint8_t *s_surface_data[2] = { NULL, NULL };
static uint16_t *s_surface_palette[2] = { NULL, NULL };
static int current_surface = 0;

const unsigned short *const d_8to16table = NULL;
const unsigned *const d_8to24table = NULL;

void VID_SetPalette(unsigned char *palette)
{
    memcpy(current_palette, palette, 256 * 3);
    palette_dirty = true;
}

void VID_ShiftPalette(unsigned char *p)
{
    VID_SetPalette(p);
}

void VID_Init(unsigned char *palette)
{
    /* Allocate double-buffered surface data in PSRAM */
    for (int i = 0; i < 2; i++) {
        s_surface_data[i] = (uint8_t *)_papp_svc->mem_caps_alloc(
            BASEWIDTH * BASEHEIGHT, PAPP_MEM_CAP_SPIRAM);
        if (!s_surface_data[i]) {
            _papp_svc->log_printf("QUAKE: Failed to allocate surface %d\n", i);
            return;
        }
        __builtin_memset(s_surface_data[i], 0, BASEWIDTH * BASEHEIGHT);

        s_surface_palette[i] = (uint16_t *)_papp_svc->mem_caps_alloc(
            256 * sizeof(uint16_t), PAPP_MEM_CAP_SPIRAM);
        if (!s_surface_palette[i]) {
            _papp_svc->log_printf("QUAKE: Failed to allocate palette %d\n", i);
            return;
        }
    }

    /* Allocate RGB565 conversion buffer */
    s_conv_buf = (uint16_t *)_papp_svc->mem_caps_alloc(
        BASEWIDTH * BASEHEIGHT * sizeof(uint16_t), PAPP_MEM_CAP_SPIRAM);
    if (!s_conv_buf) {
        _papp_svc->log_printf("QUAKE: Failed to allocate conv buffer\n");
        return;
    }

    /* Allocate Z-buffer in PSRAM */
    s_zbuffer = (int16_t *)_papp_svc->mem_caps_alloc(
        BASEWIDTH * BASEHEIGHT * sizeof(int16_t), PAPP_MEM_CAP_SPIRAM);
    if (!s_zbuffer) {
        _papp_svc->log_printf("QUAKE: Failed to allocate zbuffer\n");
        return;
    }

    /* Allocate surfcache in PSRAM */
    s_surfcache = (uint8_t *)_papp_svc->mem_caps_alloc(
        SURFCACHE_SIZE, PAPP_MEM_CAP_SPIRAM);
    if (!s_surfcache) {
        _papp_svc->log_printf("QUAKE: Failed to allocate surfcache\n");
        return;
    }

    /* Set up vid structure */
    vid.width = vid.conwidth = BASEWIDTH;
    vid.height = vid.conheight = BASEHEIGHT;
    vid.rowbytes = vid.conrowbytes = BASEWIDTH;
    vid.aspect = 1.0;
    vid.numpages = 2;
    vid.colormap = host_colormap;
    vid.fullbright = 256 - LittleLong(*((int *)vid.colormap + 2048));
    vid.buffer = vid.conbuffer = s_surface_data[current_surface];
    vid.maxwarpwidth = WARP_WIDTH;
    vid.maxwarpheight = WARP_HEIGHT;
    vid.recalc_refdef = 1;

    d_pzbuffer = (short *)s_zbuffer;
    D_InitCaches(s_surfcache, SURFCACHE_SIZE);

    VID_SetPalette(palette);

    _papp_svc->log_printf("QUAKE VID_Init: %dx%d, zbuf=%p, surfcache=%p (%dKB)\n",
                          BASEWIDTH, BASEHEIGHT, s_zbuffer, s_surfcache,
                          SURFCACHE_SIZE / 1024);
    _papp_svc->log_printf("QUAKE VID_Init: returning now...\n");
}

void VID_Shutdown(void)
{
    /* Memory freed by papp_cleanup_heap */
}

void VID_Update(vrect_t *rects)
{
    uint8_t *surf_data = s_surface_data[current_surface];

    /* Update palette if dirty */
    if (palette_dirty) {
        for (int i = 0; i < 256; i++) {
            /* Apply gamma 0.5 brightness boost directly in LUT */
            int r = current_palette[i * 3];
            int g = current_palette[i * 3 + 1];
            int b = current_palette[i * 3 + 2];
            /* Gamma 0.5 = sqrt: brightens dark pixels significantly */
            r = (int)(sqrtf((float)r / 255.0f) * 255.0f + 0.5f);
            g = (int)(sqrtf((float)g / 255.0f) * 255.0f + 0.5f);
            b = (int)(sqrtf((float)b / 255.0f) * 255.0f + 0.5f);
            if (r > 255) r = 255;
            if (g > 255) g = 255;
            if (b > 255) b = 255;
            /* RGB565 little-endian (native) — no byte-swap needed for PPA */
            palette_lut[i] = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
        }
        palette_dirty = false;
    }

    /* Convert 8bpp indexed → RGB565 using palette LUT */
    for (int y = 0; y < BASEHEIGHT; y++) {
        const uint8_t *src = surf_data + y * BASEWIDTH;
        uint16_t *dst = s_conv_buf + y * BASEWIDTH;
        for (int x = 0; x < BASEWIDTH; x++) {
            dst[x] = palette_lut[src[x]];
        }
    }

    /* Submit via PPA engine: 320×240 input, scale 2.0 to fill 480×640
       (LCD is 480×800 portrait, rotated 270°: max width = 480/240 = 2.0) */
    _papp_svc->display_write_frame_custom(s_conv_buf, BASEWIDTH, BASEHEIGHT,
                                          2.0f, false);

    /* Swap to the other surface for next frame */
    current_surface = 1 - current_surface;
    vid.buffer = vid.conbuffer = s_surface_data[current_surface];
}

void D_BeginDirectRect(int x, int y, byte *pbitmap, int width, int height)
{
}

void D_EndDirectRect(int x, int y, int width, int height)
{
}
