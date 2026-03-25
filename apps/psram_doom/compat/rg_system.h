/*
 * Minimal retro-go stub header for PSRAM Doom app.
 * Provides types and constants that prboom's source references via <rg_system.h>.
 * Actual implementations are in papp_rg_stubs.c.
 */
#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>

/* ── Key bitmasks (matches retro-go rg_input.h) ────────────────────── */
typedef enum {
    RG_KEY_UP      = (1 << 0),
    RG_KEY_RIGHT   = (1 << 1),
    RG_KEY_DOWN    = (1 << 2),
    RG_KEY_LEFT    = (1 << 3),
    RG_KEY_SELECT  = (1 << 4),
    RG_KEY_START   = (1 << 5),
    RG_KEY_MENU    = (1 << 6),
    RG_KEY_OPTION  = (1 << 7),
    RG_KEY_A       = (1 << 8),
    RG_KEY_B       = (1 << 9),
    RG_KEY_X       = (1 << 10),
    RG_KEY_Y       = (1 << 11),
    RG_KEY_L       = (1 << 12),
    RG_KEY_R       = (1 << 13),
    RG_KEY_COUNT   = 14,
    RG_KEY_ANY     = 0xFFFF,
    RG_KEY_ALL     = 0xFFFF,
    RG_KEY_NONE    = 0,
} rg_key_t;

/* ── Pixel format ──────────────────────────────────────────────────── */
#define RG_PIXEL_565_BE      0x01
#define RG_PIXEL_565_LE      0x02
#define RG_PIXEL_PALETTE     0x80
#define RG_PIXEL_PAL565_BE   (RG_PIXEL_565_BE | RG_PIXEL_PALETTE)
#define RG_PIXEL_PAL565_LE   (RG_PIXEL_565_LE | RG_PIXEL_PALETTE)
#define RG_SCREEN_PIXEL_FORMAT 0  /* selects PAL565_BE */

/* ── Audio types ───────────────────────────────────────────────────── */
typedef struct {
    int16_t left;
    int16_t right;
} rg_audio_sample_t;

/* ── Surface type ──────────────────────────────────────────────────── */
typedef struct {
    int width, height;
    int stride, offset;
    int format;
    uint16_t *palette;
    void *data;
    bool free_data;
    bool free_palette;
} rg_surface_t;

/* ── App type ──────────────────────────────────────────────────────── */
typedef struct {
    const char *romPath;
} rg_app_t;

/* ── Config / Handlers ─────────────────────────────────────────────── */
typedef struct { int unused; } rg_handlers_t;
typedef struct {
    int sampleRate;
    int frameRate;
    bool storageRequired;
    bool romRequired;
    struct {
        void *loadState;
        void *saveState;
        void *reset;
        void *screenshot;
        void *event;
        void *options;
    } handlers;
    int mallocAlwaysInternal;
} rg_config_t;

/* ── Events / Dialogs ──────────────────────────────────────────────── */
typedef int rg_gui_event_t;
typedef struct {
    int id;
    const char *label;
    char value[32];
    int flags;
    void *callback;
} rg_gui_option_t;

#define RG_DIALOG_VOID     0
#define RG_DIALOG_PREV     1
#define RG_DIALOG_NEXT     2
#define RG_DIALOG_REDRAW   3
#define RG_DIALOG_FLAG_NORMAL 0
#define RG_DIALOG_END      {0}

#define RG_EVENT_SHUTDOWN  1
#define RG_EVENT_REDRAW    2
#define RG_EVENT_GEOMETRY  3

/* ── Settings namespace ────────────────────────────────────────────── */
#define NS_APP "app"

/* ── Paths ─────────────────────────────────────────────────────────── */
#define RG_BASE_PATH_ROMS   "/sd/roms"
#define RG_BASE_PATH_SAVES  "/sd/saves"

/* ── Memory flags ──────────────────────────────────────────────────── */
#define MEM_FAST   0
#define MEM_SLOW   1
#define MEM_DMA    2

/* ── Task priority ─────────────────────────────────────────────────── */
#define RG_TASK_PRIORITY_1  3
#define RG_TASK_PRIORITY_2  5

/* ── Utilities ─────────────────────────────────────────────────────── */
#define RG_COUNT(array) (sizeof(array) / sizeof((array)[0]))
#define RG_MIN(a, b) ({__typeof__(a) _a=(a); __typeof__(b) _b=(b); _a<_b?_a:_b;})
#define RG_MAX(a, b) ({__typeof__(a) _a=(a); __typeof__(b) _b=(b); _a>_b?_a:_b;})
#define RG_PANIC(msg)  do { extern void papp_panic(const char *); papp_panic(msg); } while(0)

/* ── Stub function declarations ────────────────────────────────────── */
#define RG_FILE_USER_BUFFER 0

rg_app_t    *rg_system_init(const rg_config_t *config);
void         rg_system_exit(void) __attribute__((noreturn));
int64_t      rg_system_timer(void);
rg_app_t    *rg_system_get_app(void);
int          rg_system_get_app_speed(void);
void         rg_system_tick(int64_t elapsed);

rg_surface_t *rg_surface_create(int width, int height, int format, uint32_t alloc_flags);
void          rg_surface_free(rg_surface_t *surface);
bool          rg_surface_save_image_file(rg_surface_t *s, const char *fn, int w, int h);

void          rg_display_submit(rg_surface_t *update, int flags);
bool          rg_display_is_busy(void);
int           rg_display_get_width(void);
int           rg_display_get_height(void);

uint32_t      rg_input_read_gamepad(void);

void          rg_audio_submit(rg_audio_sample_t *buf, size_t count);
void          rg_audio_set_mute(bool mute);

void          rg_task_create(const char *name, void (*fn)(void*), void *arg,
                             int stack, int affinity, int priority, int core);
void          rg_task_yield(void);
void          rg_usleep(unsigned long us);

int32_t       rg_settings_get_number(const char *ns, const char *key, int32_t def);
void          rg_settings_set_number(const char *ns, const char *key, int32_t val);

void          rg_gui_options_menu(void);
void          rg_gui_game_menu(void);
void          rg_gui_alert(const char *title, const char *msg);
void          rg_gui_draw_hourglass(void);
const char   *rg_gui_file_picker(const char *title, const char *path,
                                  bool (*filter)(const char*), bool a, bool b);

bool          rg_extension_match(const char *path, const char *ext);
bool          rg_storage_read_file(const char *path, void **data, size_t *len, int flags);
bool          rg_storage_unzip_file(const char *path, const char *entry,
                                     void **data, size_t *len, int flags);

/* Localization stub */
#define _(s) (s)

/* vlog */
void rg_system_vlog(int level, const char *tag, const char *fmt, __builtin_va_list args);
#define RG_LOG_PRINTF 0
