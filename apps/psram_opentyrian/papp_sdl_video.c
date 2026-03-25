/*
 * SDL Video shim for PSRAM App — routes through app_services_t.
 * Replaces components/opentyrian_sdl/SDL_video.c
 */
#include "psram_app.h"
#include "SDL_video.h"
#include <string.h>
#include <stdlib.h>

extern const app_services_t *_papp_svc;

/* Global palette: 8-bit index → native RGB565 */
int16_t lcdpal[256];

SDL_Surface *primary_surface = NULL;

int SDL_LockSurface(SDL_Surface *surface) { return 0; }
void SDL_UnlockSurface(SDL_Surface *surface) {}

void SDL_UpdateRect(SDL_Surface *screen, Sint32 x, Sint32 y, Sint32 w, Sint32 h)
{
    SDL_Flip(screen);
}

SDL_VideoInfo *SDL_GetVideoInfo(void)
{
    SDL_VideoInfo *info = _papp_svc->mem_calloc(1, sizeof(SDL_VideoInfo));
    return info;
}

char *SDL_VideoDriverName(char *namebuf, int maxlen) { return "PSRAM-App P4"; }

SDL_Rect **SDL_ListModes(SDL_PixelFormat *format, Uint32 flags)
{
    static SDL_Rect mode = {0, 0, 320, 200};
    static SDL_Rect *modes[] = {&mode, NULL};
    return modes;
}

void SDL_WM_SetCaption(const char *title, const char *icon) {}
char *SDL_GetKeyName(SDLKey key) { return (char *)""; }
SDL_Keymod SDL_GetModState(void) { return (SDL_Keymod)0; }

Uint32 SDL_GetTicks(void)
{
    return (Uint32)(_papp_svc->get_time_us() / 1000);
}

Uint32 SDL_WasInit(Uint32 flags) { return 0; }

void spi_lcd_clear(void)
{
    _papp_svc->display_clear(0x0000);
    _papp_svc->display_flush();
}

void return_to_launcher(void)
{
    extern void app_return_to_launcher(void);
    app_return_to_launcher();
}

int SDL_InitSubSystem(Uint32 flags) { return 0; }

SDL_Surface *SDL_CreateRGBSurface(Uint32 flags, int width, int height, int depth,
                                   Uint32 Rmask, Uint32 Gmask, Uint32 Bmask, Uint32 Amask)
{
    SDL_Surface *surface = (SDL_Surface *)_papp_svc->mem_calloc(1, sizeof(SDL_Surface));
    SDL_Rect rect = {.x = 0, .y = 0, .w = width, .h = height};
    SDL_Color col = {.r = 0, .g = 0, .b = 0, .unused = 0};
    SDL_Palette pal = {.ncolors = 1, .colors = &col};
    SDL_PixelFormat *pf = (SDL_PixelFormat *)_papp_svc->mem_calloc(1, sizeof(SDL_PixelFormat));
    pf->palette = &pal;
    pf->BitsPerPixel = 8;
    pf->BytesPerPixel = 1;

    surface->flags = flags;
    surface->format = pf;
    surface->w = width;
    surface->h = height;
    surface->pitch = width * (depth / 8);
    surface->clip_rect = rect;
    surface->refcount = 1;
    /* Allocate from PSRAM */
    surface->pixels = _papp_svc->mem_caps_alloc(width * height, PAPP_MEM_CAP_SPIRAM);
    if (surface->pixels == NULL)
        surface->pixels = _papp_svc->mem_alloc(width * height);
    if (surface->pixels != NULL)
        memset(surface->pixels, 0, width * height);

    if (primary_surface == NULL)
        primary_surface = surface;
    return surface;
}

int SDL_FillRect(SDL_Surface *dst, SDL_Rect *dstrect, Uint32 color)
{
    if (dst != NULL) {
        if (dstrect != NULL) {
            for (int y = dstrect->y; y < dstrect->y + dstrect->h; y++)
                memset((unsigned char *)dst->pixels + y * 320 + dstrect->x,
                       (unsigned char)color, dstrect->w);
        } else {
            memset(dst->pixels, (unsigned char)color, dst->pitch * dst->h);
        }
    }
    return 0;
}

SDL_Surface *SDL_GetVideoSurface(void) { return primary_surface; }

Uint32 SDL_MapRGB(SDL_PixelFormat *fmt, Uint8 r, Uint8 g, Uint8 b)
{
    if (fmt->BitsPerPixel == 16) {
        uint16_t bb = (b >> 3) & 0x1f;
        uint16_t gg = ((g >> 2) & 0x3f) << 5;
        uint16_t rr = ((r >> 3) & 0x1f) << 11;
        return (Uint32)(rr | gg | bb);
    }
    return 0;
}

int SDL_SetColors(SDL_Surface *surface, SDL_Color *colors, int firstcolor, int ncolors)
{
    for (int i = firstcolor; i < firstcolor + ncolors; i++) {
        lcdpal[i] = ((colors[i].r >> 3) << 11) | ((colors[i].g >> 2) << 5) | (colors[i].b >> 3);
    }
    return 1;
}

SDL_Surface *SDL_SetVideoMode(int width, int height, int bpp, Uint32 flags)
{
    return SDL_GetVideoSurface();
}

void SDL_FreeSurface(SDL_Surface *surface)
{
    if (surface) {
        _papp_svc->mem_free(surface->pixels);
        _papp_svc->mem_free(surface->format);
        surface->refcount = 0;
        _papp_svc->mem_free(surface);
    }
}

void SDL_QuitSubSystem(Uint32 flags) {}

int SDL_Flip(SDL_Surface *screen)
{
    if (screen == NULL || screen->pixels == NULL)
        return -1;

    /* Convert 8bpp indexed → RGB565 into a temporary buffer, then
       hand it to display_write_frame_custom which does PPA rotate+scale
       with the correct input dimensions (not the full 800×480 FB). */
    static uint16_t *s_conv_buf = NULL;
    const int game_w = (screen->w < 320) ? screen->w : 320;
    const int game_h = (screen->h < 240) ? screen->h : 240;

    if (!s_conv_buf) {
        s_conv_buf = (uint16_t *)_papp_svc->mem_caps_alloc(
            320 * 240 * sizeof(uint16_t), PAPP_MEM_CAP_SPIRAM);
        if (!s_conv_buf) return -1;
    }

    const uint8_t *src = (const uint8_t *)screen->pixels;
    for (int y = 0; y < game_h; y++) {
        uint16_t *dst_row = s_conv_buf + y * 320;
        const uint8_t *src_row = src + y * screen->w;
        for (int x = 0; x < game_w; x++) {
            dst_row[x] = (uint16_t)lcdpal[src_row[x]];
        }
        if (game_w < 320)
            memset(dst_row + game_w, 0, (320 - game_w) * sizeof(uint16_t));
    }
    /* Zero remaining rows if game_h < 240 */
    if (game_h < 240)
        memset(s_conv_buf + game_h * 320, 0, (240 - game_h) * 320 * sizeof(uint16_t));

    /* Use custom path: 320×200 input, scale 2.4, rotate 270° → 480×768 on LCD */
    _papp_svc->display_write_frame_custom(s_conv_buf, 320, game_h, 2.4f, false);

    return 0;
}

int SDL_VideoModeOK(int width, int height, int bpp, Uint32 flags)
{
    if (bpp == 8) return 1;
    return 0;
}

/* Display mutex — use service lock/unlock */
SemaphoreHandle_t display_mutex = NULL; /* not actually used as semaphore */

void SDL_LockDisplay()
{
    _papp_svc->display_lock();
}

void SDL_UnlockDisplay()
{
    _papp_svc->display_unlock();
}
