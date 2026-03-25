#include "rg_system.h"
#include "quakedef.h"
#include "d_local.h"
#include "esp_attr.h"

// viddef_t vid; // Defined in screen.c

// Supported resolutions guide:
// - 320x240: Standard Full Screen (Recommended for P4/S3)
// - 320x200: Classic DOS Resolution (15-20% boost on S3)
// - 160x120: Half-res (Required for original ESP32 level loading)
// - 120x90: Ultra-low (Maximum stability for original ESP32)
// Note: High resolutions consume PSRAM window space (Z-buffer + Surfaces).
#if defined(CONFIG_IDF_TARGET_ESP32P4)
#define BASEWIDTH 320
#define BASEHEIGHT 240
#elif defined(CONFIG_IDF_TARGET_ESP32S3)
#define BASEWIDTH 320
#define BASEHEIGHT 240
#else // Original ESP32
#define BASEWIDTH 160
#define BASEHEIGHT 120
#endif

#if defined(CONFIG_IDF_TARGET_ESP32)
static DRAM_ATTR int16_t zbuffer[BASEWIDTH * BASEHEIGHT] __attribute__((aligned(16)));
#else
static EXT_RAM_BSS_ATTR int16_t zbuffer[BASEWIDTH * BASEHEIGHT] __attribute__((aligned(16)));
#endif

// surfcache is large. For original ESP32, keep it in internal DRAM to save PSRAM address space.
#if defined(CONFIG_IDF_TARGET_ESP32P4)
#define SURFCACHE_SIZE (640 * 1024)
static EXT_RAM_BSS_ATTR uint8_t surfcache[SURFCACHE_SIZE] __attribute__((aligned(16)));
#elif defined(CONFIG_IDF_TARGET_ESP32S3)
#define SURFCACHE_SIZE (512 * 1024)
static EXT_RAM_BSS_ATTR uint8_t surfcache[SURFCACHE_SIZE] __attribute__((aligned(16)));
#else
#define SURFCACHE_SIZE (32 * 1024)
static uint8_t surfcache[SURFCACHE_SIZE] __attribute__((aligned(16)));
#endif

const unsigned short *const d_8to16table = NULL;
const unsigned *const d_8to24table = NULL;

static rg_surface_t *rg_surfaces[2];
static int current_surface = 0;
static uint8_t current_palette[256 * 3];
static bool palette_dirty[2] = {false, false};

void VID_SetPalette(unsigned char *palette)
{
    memcpy(current_palette, palette, 256 * 3);
    palette_dirty[0] = true;
    palette_dirty[1] = true;
}

void VID_ShiftPalette(unsigned char *p)
{
    VID_SetPalette(p);
}

void VID_Init(unsigned char *palette)
{
    // Create two surfaces for double-buffering. Use internal RAM for original ESP32.
    for (int i = 0; i < 2; i++) {
#if defined(CONFIG_IDF_TARGET_ESP32)
        rg_surfaces[i] = rg_surface_create(BASEWIDTH, BASEHEIGHT, RG_PIXEL_PAL565_BE, MEM_FAST);
#else
        rg_surfaces[i] = rg_surface_create(BASEWIDTH, BASEHEIGHT, RG_PIXEL_PAL565_BE, MEM_SLOW);
#endif
        if (!rg_surfaces[i]) {
            Sys_Error("VID_Init: Could not create surface %d", i);
        }
    }

    vid.width = vid.conwidth = BASEWIDTH;
    vid.height = vid.conheight = BASEHEIGHT;
    vid.rowbytes = vid.conrowbytes = BASEWIDTH;
    vid.aspect = 1.0; 
    vid.numpages = 2;
    vid.colormap = host_colormap;
    vid.fullbright = 256 - LittleLong(*((int *)vid.colormap + 2048));
    vid.buffer = vid.conbuffer = rg_surfaces[current_surface]->data;
    vid.maxwarpwidth = WARP_WIDTH;
    vid.maxwarpheight = WARP_HEIGHT;
    vid.recalc_refdef = 1;

    d_pzbuffer = (short *)zbuffer;
    D_InitCaches(surfcache, sizeof(surfcache));
    
    // Force full screen scaling
    rg_display_set_scaling(RG_DISPLAY_SCALING_FULL);

    RG_LOGI("VID_Init: %dx%d, double-buffering, zbuffer at %p (DRAM), surfaces at %p and %p", 
            BASEWIDTH, BASEHEIGHT, zbuffer, rg_surfaces[0]->data, rg_surfaces[1]->data);
}

void VID_Shutdown(void)
{
    for (int i = 0; i < 2; i++) {
        if (rg_surfaces[i]) {
            rg_surface_free(rg_surfaces[i]);
            rg_surfaces[i] = NULL;
        }
    }
}

void VID_Update(vrect_t *rects)
{
    rg_surface_t *surf = rg_surfaces[current_surface];

    if (palette_dirty[current_surface])
    {
        for (int i = 0; i < 256; i++)
        {
            uint8_t r = current_palette[i * 3];
            uint8_t g = current_palette[i * 3 + 1];
            uint8_t b = current_palette[i * 3 + 2];
            uint16_t color = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
            surf->palette[i] = (color << 8) | (color >> 8);
        }
        palette_dirty[current_surface] = false;
    }

    // Submit current surface
    rg_display_submit(surf, 0);

    // Swap to the other surface for next frame
    current_surface = 1 - current_surface;
    vid.buffer = vid.conbuffer = rg_surfaces[current_surface]->data;

    // Synchronize with display task: wait until the NEXT surface is no longer busy
    while (rg_display_is_busy()) {
        rg_task_delay(1);
    }
}

void D_BeginDirectRect(int x, int y, byte *pbitmap, int width, int height)
{
}

void D_EndDirectRect(int x, int y, int width, int height)
{
}
