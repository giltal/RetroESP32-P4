/*
 * SDL Video shim for Duke3D PSRAM App.
 *
 * Replaces the original SDL video layer.
 * - SDL_SetVideoMode allocates an 8bpp surface in PSRAM
 * - SDL_SetColors builds a palette LUT (indexed→RGB565)
 * - SDL_UpdateRect / SDL_Flip converts and pushes to the display
 * - spi_lcd_send_boarder used as the old ESP32 frame output path
 *
 * Duke3D's engine renders into surface->pixels (8-bit indexed, 320×200).
 * VBE_setPalette() converts 6-bit BUILD palette to 8-bit SDL_Color and
 * calls SDL_SetColors() which updates our lcdpal[] RGB565 LUT.
 * _nextpage() calls SDL_UpdateRect(surface,0,0,0,0) which we route to
 * SDL_Flip() → 8bpp→RGB565 conversion → display_write_frame_custom().
 */
#include "psram_app.h"
#include "SDL.h"
#include <string.h>
#include <math.h>

extern const app_services_t *_papp_svc;

/* Global palette LUT: 8-bit index → native RGB565 */
int16_t lcdpal[256];

/* The primary surface — pointed to by display.c's `surface` global */
SDL_Surface *primary_surface = NULL;

/* Conversion buffer for 8bpp → RGB565 */
static uint16_t *s_conv_buf = NULL;

/* Persistent pixel format and palette for the primary surface */
static SDL_PixelFormat s_pixel_format;
static SDL_Palette s_palette;
static SDL_Color s_palette_colors[256];

/* ── Surface management ──────────────────────────────────────────────── */

int SDL_LockSurface(SDL_Surface *surface) { return 0; }
void SDL_UnlockSurface(SDL_Surface *surface) {}

SDL_Surface *SDL_SetVideoMode(int width, int height, int bpp, Uint32 flags)
{
    /* If already allocated at the right size, just return it */
    if (primary_surface && primary_surface->w == width && primary_surface->h == height)
        return primary_surface;

    /* Allocate surface struct */
    SDL_Surface *surf = (SDL_Surface *)_papp_svc->mem_calloc(1, sizeof(SDL_Surface));
    if (!surf) return NULL;

    /* Set up persistent pixel format with palette */
    memset(&s_pixel_format, 0, sizeof(s_pixel_format));
    memset(&s_palette, 0, sizeof(s_palette));
    memset(s_palette_colors, 0, sizeof(s_palette_colors));
    s_palette.ncolors = 256;
    s_palette.colors = s_palette_colors;
    s_pixel_format.palette = &s_palette;
    s_pixel_format.BitsPerPixel = 8;
    s_pixel_format.BytesPerPixel = 1;

    surf->flags = flags;
    surf->format = &s_pixel_format;
    surf->w = width;
    surf->h = height;
    surf->pitch = width;
    surf->clip_rect.x = 0;
    surf->clip_rect.y = 0;
    surf->clip_rect.w = width;
    surf->clip_rect.h = height;
    surf->refcount = 1;

    /* Allocate pixel buffer in PSRAM (320×200 = 64KB) */
    surf->pixels = _papp_svc->mem_caps_alloc(width * height, PAPP_MEM_CAP_SPIRAM);
    if (!surf->pixels)
        surf->pixels = _papp_svc->mem_alloc(width * height);
    if (surf->pixels)
        memset(surf->pixels, 0, width * height);

    /* Allocate RGB565 conversion buffer (320×200 × 2 = 128KB) */
    if (!s_conv_buf) {
        s_conv_buf = (uint16_t *)_papp_svc->mem_caps_alloc(
            320 * 200 * sizeof(uint16_t), PAPP_MEM_CAP_SPIRAM);
    }

    primary_surface = surf;
    _papp_svc->log_printf("Duke3D SDL_SetVideoMode: %dx%d bpp=%d\n", width, height, bpp);
    return surf;
}

SDL_Surface *SDL_GetVideoSurface(void) { return primary_surface; }

SDL_Surface *SDL_CreateRGBSurface(Uint32 flags, int width, int height, int depth,
                                   Uint32 Rmask, Uint32 Gmask, Uint32 Bmask, Uint32 Amask)
{
    SDL_Surface *surface = (SDL_Surface *)_papp_svc->mem_calloc(1, sizeof(SDL_Surface));
    if (!surface) return NULL;

    SDL_PixelFormat *pf = (SDL_PixelFormat *)_papp_svc->mem_calloc(1, sizeof(SDL_PixelFormat));
    pf->BitsPerPixel = depth;
    pf->BytesPerPixel = depth / 8;
    pf->palette = &s_palette;

    surface->flags = flags;
    surface->format = pf;
    surface->w = width;
    surface->h = height;
    surface->pitch = width * (depth / 8);
    surface->clip_rect.x = 0;
    surface->clip_rect.y = 0;
    surface->clip_rect.w = width;
    surface->clip_rect.h = height;
    surface->refcount = 1;
    surface->pixels = _papp_svc->mem_caps_alloc(width * height * (depth / 8), PAPP_MEM_CAP_SPIRAM);
    if (!surface->pixels)
        surface->pixels = _papp_svc->mem_alloc(width * height * (depth / 8));
    if (surface->pixels)
        memset(surface->pixels, 0, width * height * (depth / 8));

    if (primary_surface == NULL)
        primary_surface = surface;
    return surface;
}

void SDL_FreeSurface(SDL_Surface *surface)
{
    if (!surface) return;
    if (surface == primary_surface)
        primary_surface = NULL;
    if (surface->pixels) {
        _papp_svc->mem_free(surface->pixels);
        surface->pixels = NULL;
    }
    /* Don't free format if it points to our static one */
    if (surface->format && surface->format != &s_pixel_format) {
        _papp_svc->mem_free(surface->format);
    }
    _papp_svc->mem_free(surface);
}

/* ── Palette / color ─────────────────────────────────────────────────── */

int SDL_SetColors(SDL_Surface *surface, SDL_Color *colors, int firstcolor, int ncolors)
{
    for (int i = firstcolor; i < firstcolor + ncolors && i < 256; i++) {
        /* Store in surface palette */
        s_palette_colors[i] = colors[i];
        /* Build RGB565 LUT with gamma 0.5 brightness boost (sqrtf) */
        int r = (int)(sqrtf((float)colors[i].r / 255.0f) * 255.0f + 0.5f);
        int g = (int)(sqrtf((float)colors[i].g / 255.0f) * 255.0f + 0.5f);
        int b = (int)(sqrtf((float)colors[i].b / 255.0f) * 255.0f + 0.5f);
        if (r > 255) r = 255;
        if (g > 255) g = 255;
        if (b > 255) b = 255;
        lcdpal[i] = (int16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
    }
    return 1;
}

Uint32 SDL_MapRGB(SDL_PixelFormat *fmt, Uint8 r, Uint8 g, Uint8 b)
{
    if (fmt && fmt->BitsPerPixel == 16) {
        return (Uint32)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
    }
    return 0;
}

/* ── Frame output ────────────────────────────────────────────────────── */

int SDL_Flip(SDL_Surface *screen)
{
    if (!screen || !screen->pixels || !s_conv_buf)
        return -1;

    const int game_w = (screen->w < 320) ? screen->w : 320;
    const int game_h = (screen->h < 200) ? screen->h : 200;

    /* Convert 8bpp indexed → RGB565 via lcdpal LUT */
    const uint8_t *src = (const uint8_t *)screen->pixels;
    for (int y = 0; y < game_h; y++) {
        uint16_t *dst_row = s_conv_buf + y * 320;
        const uint8_t *src_row = src + y * screen->pitch;
        for (int x = 0; x < game_w; x++) {
            dst_row[x] = (uint16_t)lcdpal[src_row[x]];
        }
        if (game_w < 320)
            memset(dst_row + game_w, 0, (320 - game_w) * sizeof(uint16_t));
    }

    /* Scale 2.4×: 320×200 → 768×480, rotated onto 480×800 LCD */
    _papp_svc->display_write_frame_custom(s_conv_buf, 320, game_h, 2.4f, false);

    /* Yield to let IDLE task feed the watchdog */
    _papp_svc->delay_ms(1);
    return 0;
}

void SDL_UpdateRect(SDL_Surface *screen, Sint32 x, Sint32 y, Sint32 w, Sint32 h)
{
    /* Duke3D calls SDL_UpdateRect(surface, 0, 0, 0, 0) which means full update */
    SDL_Flip(screen);
}

int SDL_FillRect(SDL_Surface *dst, SDL_Rect *dstrect, Uint32 color)
{
    if (!dst || !dst->pixels) return -1;
    if (dstrect) {
        for (int y = dstrect->y; y < dstrect->y + dstrect->h && y < dst->h; y++) {
            int offset = y * dst->pitch + dstrect->x;
            int len = dstrect->w;
            if (dstrect->x + len > dst->w) len = dst->w - dstrect->x;
            memset((uint8_t *)dst->pixels + offset, (uint8_t)color, len);
        }
    } else {
        memset(dst->pixels, (uint8_t)color, dst->pitch * dst->h);
    }
    return 0;
}

int SDL_SaveBMP(SDL_Surface *surface, const char *file)
{
    return 0; /* screenshots not supported */
}

SDL_Surface *SDL_LoadBMP_RW(void *src, int freesrc)
{
    return NULL; /* BMP loading not supported */
}

/* ── spi_lcd interface (legacy engine calls) ─────────────────────────── */

void spi_lcd_send(uint16_t *scr)
{
    if (scr)
        _papp_svc->display_write_frame_custom(scr, 320, 200, 2.4f, false);
}

void spi_lcd_send_boarder(void *pixels, int border)
{
    /* Called with the 8bpp framebuffer. Convert and display. */
    if (!pixels || !s_conv_buf) return;
    const uint8_t *src = (const uint8_t *)pixels;
    for (int y = 0; y < 200; y++) {
        uint16_t *dst_row = s_conv_buf + y * 320;
        const uint8_t *src_row = src + y * 320;
        for (int x = 0; x < 320; x++) {
            dst_row[x] = (uint16_t)lcdpal[src_row[x]];
        }
    }
    _papp_svc->display_write_frame_custom(s_conv_buf, 320, 200, 2.4f, false);
}

void spi_lcd_init(void)
{
    /* LCD already initialized by launcher */
}

void spi_lcd_clear(void)
{
    _papp_svc->display_clear(0x0000);
    _papp_svc->display_flush();
}

/* ── SDL display locking (no-op, single display context) ─────────────── */
void SDL_LockDisplay(void) {}
void SDL_UnlockDisplay(void) {}

/* ── Misc SDL video stubs ────────────────────────────────────────────── */

SDL_VideoInfo *SDL_GetVideoInfo(void)
{
    static SDL_VideoInfo info;
    memset(&info, 0, sizeof(info));
    return &info;
}

char *SDL_VideoDriverName(char *namebuf, int maxlen)
{
    if (namebuf && maxlen > 0) {
        strncpy(namebuf, "PAPP-P4", maxlen - 1);
        namebuf[maxlen - 1] = '\0';
    }
    return namebuf;
}

SDL_Rect **SDL_ListModes(SDL_PixelFormat *format, Uint32 flags)
{
    static SDL_Rect mode = {0, 0, 320, 200};
    static SDL_Rect *modes[] = { &mode, NULL };
    return modes;
}

void SDL_WM_SetCaption(const char *title, const char *icon) {}
void SDL_WM_SetIcon(SDL_Surface *icon, Uint8 *mask) {}
SDL_GrabMode SDL_WM_GrabInput(SDL_GrabMode mode) { return mode; }
int SDL_ShowCursor(int toggle) { return 0; }

void SDL_QuitSubSystem(Uint32 flags) {}
int SDL_InitSubSystem(Uint32 flags) { return 0; }
