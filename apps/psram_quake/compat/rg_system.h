/*
 * Minimal retro-go stub header for PSRAM Quake app.
 * Provides types and constants that the Quake source references via <rg_system.h>.
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
#define RG_SCREEN_PIXEL_FORMAT 0

/* ── Audio types ───────────────────────────────────────────────────── */
typedef struct {
    int16_t left;
    int16_t right;
} rg_audio_sample_t;

typedef rg_audio_sample_t rg_audio_frame_t;

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

/* ── Display scaling ───────────────────────────────────────────────── */
#define RG_DISPLAY_SCALING_FULL 2

/* ── Memory allocation flags ───────────────────────────────────────── */
#define MEM_SLOW  0
#define MEM_FAST  1
#define MEM_ANY   2

/* ── Logging macros ────────────────────────────────────────────────── */
#define RG_LOG_ERROR 0
#define RG_LOG_WARN  1
#define RG_LOG_INFO  2
#define RG_LOG_DEBUG 3

void rg_system_vlog(int level, const char *tag, const char *fmt, __builtin_va_list args);

#define RG_LOGE(fmt, ...) do { /* discard */ } while(0)
#define RG_LOGW(fmt, ...) do { /* discard */ } while(0)
#define RG_LOGI(fmt, ...) do { /* discard */ } while(0)
#define RG_LOGD(fmt, ...) do { /* discard */ } while(0)

/* ── Task priority ─────────────────────────────────────────────────── */
#define RG_TASK_PRIORITY_5 5
#define RG_TASK_PRIORITY_8 8

/* ── Storage paths (used by quake for save/config dirs) ────────────── */
#define RG_BASE_PATH_ROMS    "/sd/roms"
#define RG_BASE_PATH_SAVES   "/sd/saves"
#define RG_BASE_PATH_CONFIG  "/sd/config"

/* ── Functions provided by stubs ───────────────────────────────────── */
void rg_display_submit(rg_surface_t *update, int flags);
bool rg_display_is_busy(void);
void rg_display_set_scaling(int mode);
uint32_t rg_input_read_gamepad(void);
void rg_audio_submit(rg_audio_sample_t *buf, size_t count);
void rg_audio_set_mute(bool mute);
int rg_audio_get_sample_rate(void);
void rg_task_yield(void);
void rg_task_delay(int ms);
rg_app_t *rg_system_init(const rg_config_t *config);
void rg_system_exit(void);
int64_t rg_system_timer(void);
rg_surface_t *rg_surface_create(int w, int h, int fmt, uint32_t flags);
void rg_surface_free(rg_surface_t *s);
bool rg_storage_mkdir(const char *path);

/* Panic macro */
void papp_panic(const char *msg);
#define RG_PANIC(msg) papp_panic(msg)
