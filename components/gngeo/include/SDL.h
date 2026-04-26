/*
 * Minimal SDL shim for GnGeo on ESP32-P4 (bare-metal, no real SDL)
 * Provides type definitions and stub macros to replace SDL 1.2 dependencies.
 */
#ifndef _GNGEO_SDL_SHIM_H_
#define _GNGEO_SDL_SHIM_H_

#include <stdint.h>
#include <string.h>
#include <stdlib.h>

/* ── SDL integer types ── */
typedef uint8_t   Uint8;
typedef uint16_t  Uint16;
typedef uint32_t  Uint32;
typedef int8_t    Sint8;
typedef int16_t   Sint16;
typedef int32_t   Sint32;

/* ── SDL_Rect ── */
typedef struct SDL_Rect {
    int16_t x, y;
    uint16_t w, h;
} SDL_Rect;

/* ── Surface flags ── */
#define SDL_SWSURFACE   0x00000000
#define SDL_HWSURFACE   0x00000001
#define SDL_SRCCOLORKEY 0x00001000
#define SDL_FULLSCREEN  0x80000000
#define SDL_RESIZABLE   0x00000010
#define SDL_HWPALETTE   0x20000000

/* ── SDL_Surface (minimal — just a raw pixel buffer) ── */
typedef struct SDL_PixelFormat {
    uint8_t  BitsPerPixel;
    uint8_t  BytesPerPixel;
    uint32_t Rmask, Gmask, Bmask, Amask;
} SDL_PixelFormat;

typedef struct SDL_Surface {
    uint32_t flags;
    SDL_PixelFormat *format;
    int w, h;
    uint16_t pitch;
    void *pixels;
    SDL_Rect clip_rect;
} SDL_Surface;

/* ── Byte swap ── */
#define SDL_Swap16(x) __builtin_bswap16(x)
#define SDL_Swap32(x) __builtin_bswap32(x)

/* ── Surface operations (stubs / minimal impls) ── */
static inline SDL_Surface *SDL_CreateRGBSurface(uint32_t flags, int w, int h,
    int bpp, uint32_t Rm, uint32_t Gm, uint32_t Bm, uint32_t Am)
{
    (void)flags; (void)Rm; (void)Gm; (void)Bm; (void)Am;
    SDL_Surface *s = (SDL_Surface *)calloc(1, sizeof(SDL_Surface));
    if (!s) return NULL;
    SDL_PixelFormat *fmt = (SDL_PixelFormat *)calloc(1, sizeof(SDL_PixelFormat));
    if (!fmt) { free(s); return NULL; }
    fmt->BitsPerPixel  = bpp;
    fmt->BytesPerPixel = bpp / 8;
    fmt->Rmask = Rm; fmt->Gmask = Gm; fmt->Bmask = Bm; fmt->Amask = Am;
    s->format = fmt;
    s->w = w;
    s->h = h;
    s->pitch = w * (bpp / 8);
    s->pixels = calloc(1, s->pitch * h);
    if (!s->pixels) { free(fmt); free(s); return NULL; }
    s->clip_rect.x = 0; s->clip_rect.y = 0;
    s->clip_rect.w = w; s->clip_rect.h = h;
    return s;
}

static inline void SDL_FreeSurface(SDL_Surface *s) {
    if (s) { free(s->pixels); free(s->format); free(s); }
}

static inline SDL_Surface *SDL_CreateRGBSurfaceFrom(void *pixels, int w, int h,
    int bpp, int pitch, uint32_t Rm, uint32_t Gm, uint32_t Bm, uint32_t Am)
{
    SDL_Surface *s = (SDL_Surface *)calloc(1, sizeof(SDL_Surface));
    if (!s) return NULL;
    SDL_PixelFormat *fmt = (SDL_PixelFormat *)calloc(1, sizeof(SDL_PixelFormat));
    if (!fmt) { free(s); return NULL; }
    fmt->BitsPerPixel  = bpp;
    fmt->BytesPerPixel = bpp / 8;
    fmt->Rmask = Rm; fmt->Gmask = Gm; fmt->Bmask = Bm; fmt->Amask = Am;
    s->format = fmt;
    s->w = w; s->h = h;
    s->pitch = pitch;
    s->pixels = pixels;
    return s;
}

static inline int SDL_LockSurface(SDL_Surface *s) { (void)s; return 0; }
static inline void SDL_UnlockSurface(SDL_Surface *s) { (void)s; }

static inline void SDL_SetClipRect(SDL_Surface *s, const SDL_Rect *r) {
    if (s && r) s->clip_rect = *r;
}

static inline int SDL_FillRect(SDL_Surface *dst, SDL_Rect *r, uint32_t color) {
    if (!dst || !dst->pixels) return -1;
    int bpp = dst->format->BytesPerPixel;
    if (!r) {
        /* Fill entire surface — use 32-bit writes for 2× throughput */
        if (bpp == 2) {
            uint16_t c16 = (uint16_t)color;
            uint32_t c32 = ((uint32_t)c16 << 16) | c16;
            uint32_t *p32 = (uint32_t *)dst->pixels;
            int count32 = (dst->w * dst->h) >> 1;
            for (int i = 0; i < count32; i++) p32[i] = c32;
        } else {
            memset(dst->pixels, (int)color, dst->pitch * dst->h);
        }
    } else {
        if (bpp == 2) {
            uint16_t c16 = (uint16_t)color;
            uint32_t c32 = ((uint32_t)c16 << 16) | c16;
            for (int y = r->y; y < r->y + r->h && y < dst->h; y++) {
                uint16_t *row = (uint16_t *)((uint8_t *)dst->pixels + y * dst->pitch);
                int x = r->x;
                int xend = r->x + r->w;
                if (xend > dst->w) xend = dst->w;
                /* Align to 32-bit boundary */
                if ((x & 1) && x < xend) { row[x] = c16; x++; }
                /* 32-bit bulk fill */
                uint32_t *p32 = (uint32_t *)&row[x];
                int n32 = (xend - x) >> 1;
                for (int i = 0; i < n32; i++) p32[i] = c32;
                x += n32 << 1;
                /* Tail */
                if (x < xend) row[x] = c16;
            }
        }
    }
    return 0;
}

static inline int SDL_BlitSurface(SDL_Surface *src, SDL_Rect *srcrect,
    SDL_Surface *dst, SDL_Rect *dstrect)
{
    if (!src || !dst || !src->pixels || !dst->pixels) return -1;
    int sx = srcrect ? srcrect->x : 0;
    int sy = srcrect ? srcrect->y : 0;
    int sw = srcrect ? srcrect->w : src->w;
    int sh = srcrect ? srcrect->h : src->h;
    int dx = dstrect ? dstrect->x : 0;
    int dy = dstrect ? dstrect->y : 0;
    int bpp = src->format->BytesPerPixel;
    for (int y = 0; y < sh; y++) {
        uint8_t *srow = (uint8_t *)src->pixels + (sy + y) * src->pitch + sx * bpp;
        uint8_t *drow = (uint8_t *)dst->pixels + (dy + y) * dst->pitch + dx * bpp;
        memcpy(drow, srow, sw * bpp);
    }
    return 0;
}

/* ── SDL_MapRGB ── */
static inline uint32_t SDL_MapRGB(SDL_PixelFormat *fmt, uint8_t r, uint8_t g, uint8_t b) {
    if (!fmt) return 0;
    if (fmt->BitsPerPixel == 16) {
        /* RGB565 */
        return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
    }
    return (r << 16) | (g << 8) | b;
}

/* ── Timing ── */
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
static inline uint32_t SDL_GetTicks(void) {
    return (uint32_t)(esp_timer_get_time() / 1000);
}
static inline void SDL_Delay(uint32_t ms) {
    vTaskDelay(ms / portTICK_PERIOD_MS ? ms / portTICK_PERIOD_MS : 1);
}

/* ── Audio stubs ── */
typedef struct SDL_AudioSpec {
    int freq;
    uint16_t format;
    uint8_t channels;
    uint8_t silence;
    uint16_t samples;
    uint32_t size;
    void (*callback)(void *userdata, uint8_t *stream, int len);
    void *userdata;
} SDL_AudioSpec;

#define AUDIO_S16  0x8010
#define AUDIO_S16SYS AUDIO_S16

static inline void SDL_LockAudio(void) {}
static inline void SDL_UnlockAudio(void) {}

/* ── Event stubs ── */
typedef struct SDL_Event {
    int type;
} SDL_Event;

#define SDL_QUIT         0x100
#define SDL_KEYDOWN      0x300
#define SDL_KEYUP        0x301
#define SDL_JOYAXISMOTION 0x700
#define SDL_JOYBUTTONDOWN 0x703
#define SDL_JOYBUTTONUP   0x704
#define SDL_JOYHATMOTION  0x705
#define SDL_VIDEORESIZE   0x800

static inline int SDL_PollEvent(SDL_Event *e) { (void)e; return 0; }

/* ── Joystick stubs ── */
typedef struct SDL_Joystick SDL_Joystick;
#define SDL_HAT_CENTERED  0x00
#define SDL_HAT_UP        0x01
#define SDL_HAT_RIGHT     0x02
#define SDL_HAT_DOWN      0x04
#define SDL_HAT_LEFT      0x08
#define SDL_HAT_RIGHTUP   (SDL_HAT_RIGHT | SDL_HAT_UP)
#define SDL_HAT_RIGHTDOWN (SDL_HAT_RIGHT | SDL_HAT_DOWN)
#define SDL_HAT_LEFTUP    (SDL_HAT_LEFT | SDL_HAT_UP)
#define SDL_HAT_LEFTDOWN  (SDL_HAT_LEFT | SDL_HAT_DOWN)

/* ── SDLK keyboard constants (minimal set used by GnGeo) ── */
#define SDLK_LAST 512

/* ── RWops stubs ── */
typedef struct SDL_RWops SDL_RWops;
static inline SDL_RWops *SDL_RWFromMem(void *mem, int size) {
    (void)mem; (void)size; return NULL;
}
static inline void SDL_FreeRW(SDL_RWops *rw) { (void)rw; }
static inline SDL_Surface *SDL_LoadBMP_RW(SDL_RWops *src, int freesrc) {
    (void)src; (void)freesrc; return NULL;
}
static inline int SDL_SaveBMP(SDL_Surface *s, const char *file) {
    (void)s; (void)file; return -1;
}

/* ── Misc stubs ── */
static inline void sdl_set_title(char *name) { (void)name; }

#endif /* _GNGEO_SDL_SHIM_H_ */
