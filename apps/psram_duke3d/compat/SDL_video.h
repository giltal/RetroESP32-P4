/*
 * SDL_video.h compat header for Duke3D PSRAM app.
 * Contains struct definitions matching the original SDL types.
 * Function implementations are in papp_sdl_video.c.
 */
#ifndef SDL_TFT_H
#define SDL_TFT_H

#include "SDL_stdinc.h"
#include "spi_lcd.h"

typedef struct {
    Uint8 r, g, b, unused;
} SDL_Color;

typedef struct {
    int ncolors;
    SDL_Color *colors;
} SDL_Palette;

typedef struct SDL_PixelFormat {
    SDL_Palette *palette;
    Uint8 BitsPerPixel;
    Uint8 BytesPerPixel;
    Uint8 Rloss, Gloss, Bloss, Aloss;
    Uint8 Rshift, Gshift, Bshift, Ashift;
    Uint32 Rmask, Gmask, Bmask, Amask;
    Uint32 colorkey;
    Uint8 alpha;
} SDL_PixelFormat;

typedef struct {
    Sint16 x, y;
    Uint16 w, h;
} SDL_Rect;

typedef struct SDL_Surface {
    Uint32 flags;
    SDL_PixelFormat *format;
    int w, h;
    Uint16 pitch;
    void *pixels;
    SDL_Rect clip_rect;
    int refcount;
} SDL_Surface;

/* Function declarations requiring SDL_Surface */
int SDL_SaveBMP(SDL_Surface *surface, const char *file);
SDL_Surface *SDL_LoadBMP_RW(void *src, int freesrc);

/* Surface flags */
#define SDL_SWSURFACE   0x00000000
#define SDL_HWSURFACE   0x00000001
#define SDL_ASYNCBLIT   0x00000004
#define SDL_ANYFORMAT   0x10000000
#define SDL_HWPALETTE   0x20000000
#define SDL_DOUBLEBUF   0x40000000
#define SDL_FULLSCREEN  0x80000000
#define SDL_OPENGL      0x00000002
#define SDL_OPENGLBLIT  0x0000000A
#define SDL_RESIZABLE   0x00000010
#define SDL_NOFRAME     0x00000020
#define SDL_HWACCEL     0x00000100
#define SDL_SRCCOLORKEY 0x00001000
#define SDL_RLEACCELOK  0x00002000
#define SDL_RLEACCEL    0x00004000
#define SDL_SRCALPHA    0x00010000
#define SDL_PREALLOC    0x01000000

#define SDL_MUSTLOCK(S) (((S)->flags & SDL_RLEACCEL) != 0)

typedef struct {
    Uint32 hw_available:1;
    Uint32 wm_available:1;
    Uint32 blit_hw:1;
    Uint32 blit_hw_CC:1;
    Uint32 blit_hw_A:1;
    Uint32 blit_sw:1;
    Uint32 blit_sw_CC:1;
    Uint32 blit_sw_A:1;
    Uint32 blit_fill;
    Uint32 video_mem;
    SDL_PixelFormat *vfmt;
} SDL_VideoInfo;

typedef int SDLKey;

void SDL_WM_SetCaption(const char *title, const char *icon);
Uint32 SDL_WasInit(Uint32 flags);
int SDL_InitSubSystem(Uint32 flags);
int SDL_FillRect(SDL_Surface *dst, SDL_Rect *dstrect, Uint32 color);
SDL_Surface *SDL_CreateRGBSurface(Uint32 flags, int width, int height, int depth,
                                  Uint32 Rmask, Uint32 Gmask, Uint32 Bmask, Uint32 Amask);
char *SDL_GetKeyName(SDLKey key);
Uint32 SDL_GetTicks(void);
SDL_Keymod SDL_GetModState(void);
SDL_Surface *SDL_GetVideoSurface(void);
Uint32 SDL_MapRGB(SDL_PixelFormat *fmt, Uint8 r, Uint8 g, Uint8 b);
int SDL_SetColors(SDL_Surface *surface, SDL_Color *colors, int firstcolor, int ncolors);
SDL_Surface *SDL_SetVideoMode(int width, int height, int bpp, Uint32 flags);
void SDL_FreeSurface(SDL_Surface *surface);
void SDL_QuitSubSystem(Uint32 flags);
int SDL_Flip(SDL_Surface *screen);
int SDL_VideoModeOK(int width, int height, int bpp, Uint32 flags);
int SDL_LockSurface(SDL_Surface *surface);
void SDL_UnlockSurface(SDL_Surface *surface);
void SDL_UpdateRect(SDL_Surface *screen, Sint32 x, Sint32 y, Sint32 w, Sint32 h);
SDL_Rect **SDL_ListModes(SDL_PixelFormat *format, Uint32 flags);
SDL_VideoInfo *SDL_GetVideoInfo(void);
char *SDL_VideoDriverName(char *namebuf, int maxlen);

/* Display locking (no-ops in papp) */
void SDL_LockDisplay(void);
void SDL_UnlockDisplay(void);

typedef unsigned char JE_byte;

/* SDL_RWops stub */
typedef struct SDL_RWops {
    void *data;
} SDL_RWops;
SDL_RWops *SDL_RWFromMem(void *mem, int size);

/* Window manager — SDL_GrabMode defined in SDL_input.h */
void SDL_WM_SetIcon(SDL_Surface *icon, Uint8 *mask);
int SDL_ShowCursor(int toggle);

#endif /* SDL_TFT_H */
