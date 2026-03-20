/*
 * SDL Video shim for ESP32-P4
 *
 * Replaces the SPI ILI9341 backend with the P4 MIPI DSI display.
 * Game renders 320×200 8bpp indexed → convert to RGB565 → write
 * centered in 320×240 framebuffer with 20-pixel black border.
 */
#include "SDL_video.h"
#include "odroid_display.h"
#include "esp_timer.h"
#include <string.h>
#include <stdlib.h>

/* Global palette: 8-bit index → native RGB565 (no byte-swap on P4) */
int16_t lcdpal[256];

/* RGB565 line buffer for 8bpp→RGB565 conversion */
static uint16_t *line_buf = NULL;

SDL_Surface *primary_surface = NULL;

int SDL_LockSurface(SDL_Surface *surface)
{
    return 0;
}

void SDL_UnlockSurface(SDL_Surface *surface)
{
}

void SDL_UpdateRect(SDL_Surface *screen, Sint32 x, Sint32 y, Sint32 w, Sint32 h)
{
    SDL_Flip(screen);
}

SDL_VideoInfo *SDL_GetVideoInfo(void)
{
    SDL_VideoInfo *info = calloc(1, sizeof(SDL_VideoInfo));
    return info;
}

char *SDL_VideoDriverName(char *namebuf, int maxlen)
{
    return "ESP32-P4 MIPI DSI";
}

SDL_Rect **SDL_ListModes(SDL_PixelFormat *format, Uint32 flags)
{
    static SDL_Rect mode = {0, 0, 320, 200};
    static SDL_Rect *modes[] = {&mode, NULL};
    return modes;
}

void SDL_WM_SetCaption(const char *title, const char *icon)
{
}

char *SDL_GetKeyName(SDLKey key)
{
    return (char *)"";
}

SDL_Keymod SDL_GetModState(void)
{
    return (SDL_Keymod)0;
}

IRAM_ATTR Uint32 SDL_GetTicks(void)
{
    return (Uint32)(esp_timer_get_time() / 1000);
}

Uint32 SDL_WasInit(Uint32 flags)
{
    return 0;
}

/* Stub for old SPI LCD clear — use P4 display API */
void spi_lcd_clear(void)
{
    ili9341_clear(0x0000);
    display_flush();
}

/* Called from game code when it wants to return to launcher */
void return_to_launcher(void)
{
    extern void app_return_to_launcher(void);
    app_return_to_launcher();
}

int SDL_InitSubSystem(Uint32 flags)
{
    /* Display already initialized by app_common via app_init() */
    if (flags == SDL_INIT_VIDEO) {
        /* Allocate line conversion buffer */
        if (line_buf == NULL) {
            line_buf = heap_caps_malloc(320 * sizeof(uint16_t), MALLOC_CAP_SPIRAM);
        }
    }
    return 0;
}

SDL_Surface *SDL_CreateRGBSurface(Uint32 flags, int width, int height, int depth,
                                   Uint32 Rmask, Uint32 Gmask, Uint32 Bmask, Uint32 Amask)
{
    SDL_Surface *surface = (SDL_Surface *)calloc(1, sizeof(SDL_Surface));
    SDL_Rect rect = {.x = 0, .y = 0, .w = width, .h = height};
    SDL_Color col = {.r = 0, .g = 0, .b = 0, .unused = 0};
    SDL_Palette pal = {.ncolors = 1, .colors = &col};
    SDL_PixelFormat *pf = (SDL_PixelFormat *)calloc(1, sizeof(SDL_PixelFormat));
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
    surface->pixels = heap_caps_malloc(width * height, MALLOC_CAP_SPIRAM);
    if (surface->pixels == NULL) {
        printf("SDL_CreateRGBSurface: PSRAM alloc failed (%d bytes), falling back\n", width * height);
        surface->pixels = heap_caps_malloc(width * height, MALLOC_CAP_8BIT);
    }
    if (surface->pixels != NULL) {
        memset(surface->pixels, 0, width * height);
    } else {
        printf("SDL_CreateRGBSurface: allocation failed completely!\n");
    }
    if (primary_surface == NULL)
        primary_surface = surface;
    return surface;
}

int SDL_FillRect(SDL_Surface *dst, SDL_Rect *dstrect, Uint32 color)
{
    if (dst != NULL) {
        if (dstrect != NULL) {
            for (int y = dstrect->y; y < dstrect->y + dstrect->h; y++)
                memset((unsigned char *)dst->pixels + y * 320 + dstrect->x, (unsigned char)color, dstrect->w);
        } else {
            memset(dst->pixels, (unsigned char)color, dst->pitch * dst->h);
        }
    }
    return 0;
}

SDL_Surface *SDL_GetVideoSurface(void)
{
    return primary_surface;
}

Uint32 SDL_MapRGB(SDL_PixelFormat *fmt, Uint8 r, Uint8 g, Uint8 b)
{
    if (fmt->BitsPerPixel == 16) {
        uint16_t bb = (b >> 3) & 0x1f;
        uint16_t gg = ((g >> 2) & 0x3f) << 5;
        uint16_t rr = ((r >> 3) & 0x1f) << 11;
        return (Uint32)(rr | gg | bb);
    }
    return (Uint32)0;
}

int SDL_SetColors(SDL_Surface *surface, SDL_Color *colors, int firstcolor, int ncolors)
{
    /* Convert palette to native RGB565 (no byte-swap needed on P4) */
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
        free(surface->pixels);
        free(surface->format);
        surface->refcount = 0;
        free(surface);
    }
}

void SDL_QuitSubSystem(Uint32 flags)
{
}

int SDL_Flip(SDL_Surface *screen)
{
    /*
     * Convert 320×200 8bpp indexed surface to RGB565 and write
     * into the 320×240 framebuffer, stretching vertically with
     * nearest-neighbor scaling (200→240 = 1.2×) to fill the FB.
     */
    uint16_t *fb = display_get_framebuffer();
    if (fb == NULL || screen == NULL || screen->pixels == NULL)
        return -1;

    const uint8_t *src = (const uint8_t *)screen->pixels;
    const int game_h = (screen->h < 240) ? screen->h : 240;
    const int game_w = (screen->w < 320) ? screen->w : 320;

    /* Vertical stretch: map each of 240 output rows to a source row */
    for (int dst_y = 0; dst_y < 240; dst_y++) {
        int src_y = dst_y * game_h / 240;
        uint16_t *dst_row = fb + dst_y * 320;
        const uint8_t *src_row = src + src_y * screen->w;
        for (int x = 0; x < game_w; x++) {
            dst_row[x] = (uint16_t)lcdpal[src_row[x]];
        }
        /* Right padding if game_w < 320 */
        if (game_w < 320)
            memset(dst_row + game_w, 0, (320 - game_w) * sizeof(uint16_t));
    }

    display_flush_force();
    return 0;
}

int SDL_VideoModeOK(int width, int height, int bpp, Uint32 flags)
{
    if (bpp == 8)
        return 1;
    return 0;
}

SemaphoreHandle_t display_mutex = NULL;

void SDL_LockDisplay()
{
    if (display_mutex == NULL) {
        display_mutex = xSemaphoreCreateMutex();
        if (!display_mutex)
            abort();
    }
    if (!xSemaphoreTake(display_mutex, 60000 / portTICK_PERIOD_MS))
        abort();
}

void SDL_UnlockDisplay()
{
    if (!display_mutex)
        abort();
    if (!xSemaphoreGive(display_mutex))
        abort();
}
