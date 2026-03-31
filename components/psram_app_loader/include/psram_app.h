/*
 * PSRAM App Loader — Shared ABI Header
 *
 * This header defines the interface between the launcher and PSRAM-loaded apps.
 * It is used by both:
 *   - The launcher (to populate the service table and load/run apps)
 *   - PSRAM apps (to access launcher services via function pointers)
 *
 * ABI version must match between launcher and apps.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── .papp Binary Format ─────────────────────────────────────────────── */

#define PAPP_MAGIC       0x50415050   /* "PAPP" in little-endian */
#define PAPP_ABI_VERSION 1
#define PAPP_HEADER_SIZE 32

typedef struct __attribute__((packed)) {
    uint32_t magic;        /* Must be PAPP_MAGIC                        */
    uint32_t version;      /* ABI version (PAPP_ABI_VERSION)            */
    uint32_t entry_off;    /* Byte offset to entry fn from text start   */
    uint32_t text_size;    /* Size of .text + .rodata segment           */
    uint32_t data_size;    /* Size of .data segment (initialized)       */
    uint32_t bss_size;     /* Size of .bss segment (zero-filled)        */
    uint32_t flags;        /* Reserved, must be 0                       */
    uint32_t reserved;     /* Padding to 32 bytes                       */
} papp_header_t;

_Static_assert(sizeof(papp_header_t) == PAPP_HEADER_SIZE,
               "papp_header_t must be 32 bytes");

/* ── Gamepad State (matches odroid_gamepad_state layout) ─────────────── */

enum {
    PAPP_INPUT_UP = 0,
    PAPP_INPUT_RIGHT,
    PAPP_INPUT_DOWN,
    PAPP_INPUT_LEFT,
    PAPP_INPUT_SELECT,
    PAPP_INPUT_START,
    PAPP_INPUT_A,
    PAPP_INPUT_B,
    PAPP_INPUT_X,
    PAPP_INPUT_Y,
    PAPP_INPUT_L,
    PAPP_INPUT_R,
    PAPP_INPUT_MENU,
    PAPP_INPUT_VOLUME,
    PAPP_INPUT_MAX
};

typedef struct {
    int values[PAPP_INPUT_MAX];
} papp_gamepad_state_t;

/* ── Memory Capability Flags (matches ESP-IDF MALLOC_CAP_*) ─────────── */

#define PAPP_MEM_CAP_SPIRAM   (1 << 10)  /* MALLOC_CAP_SPIRAM */
#define PAPP_MEM_CAP_INTERNAL (1 << 11)  /* MALLOC_CAP_INTERNAL */
#define PAPP_MEM_CAP_DMA      (1 << 2)   /* MALLOC_CAP_DMA */

/* ── App Services Table ──────────────────────────────────────────────── */
/*
 * Function pointer table populated by the launcher.
 * Passed to the loaded app's entry function.
 * The PSRAM app must NOT call any function outside this table.
 */
typedef struct {
    uint32_t abi_version;   /* Must equal PAPP_ABI_VERSION */

    /* ── Display ─────────────────────────────────────────────────────── */
    uint16_t *(*display_get_framebuffer)(void);
    uint16_t *(*display_get_emu_buffer)(void);
    void      (*display_flush)(void);
    void      (*display_emu_flush)(void);
    void      (*display_clear)(uint16_t color);
    void      (*display_set_scale)(float sx, float sy);
    void      (*display_write_frame_rgb565)(const uint16_t *buffer);
    void      (*display_write_frame_custom)(const uint16_t *buffer,
                  uint16_t in_w, uint16_t in_h, float scale,
                  bool byte_swap);
    void      (*display_write_rect)(int x, int y, int w, int h,
                  const uint16_t *data);
    void      (*display_lock)(void);
    void      (*display_unlock)(void);

    /* ── Audio ───────────────────────────────────────────────────────── */
    void (*audio_init)(int sample_rate);
    void (*audio_submit)(short *stereo_buf, int frame_count);

    /* ── Input ───────────────────────────────────────────────────────── */
    void (*input_gamepad_read)(papp_gamepad_state_t *state);

    /* ── File I/O (standard C wrappers) ──────────────────────────────── */
    void  *(*file_open)(const char *path, const char *mode);
    int    (*file_close)(void *stream);
    size_t (*file_read)(void *ptr, size_t size, size_t nmemb, void *stream);
    size_t (*file_write)(const void *ptr, size_t size, size_t nmemb,
                         void *stream);
    int    (*file_seek)(void *stream, long offset, int whence);
    long   (*file_tell)(void *stream);

    /* ── Memory ──────────────────────────────────────────────────────── */
    void *(*mem_alloc)(size_t size);
    void *(*mem_calloc)(size_t n, size_t size);
    void *(*mem_realloc)(void *ptr, size_t size);
    void  (*mem_free)(void *ptr);
    void *(*mem_caps_alloc)(size_t size, uint32_t caps);

    /* ── System ──────────────────────────────────────────────────────── */
    int     (*log_printf)(const char *fmt, ...);
    int     (*log_vprintf)(const char *fmt, va_list args);
    void    (*delay_ms)(int ms);
    int64_t (*get_time_us)(void);

    /* ── Settings (NVS) ──────────────────────────────────────────────── */
    char   *(*settings_rom_path_get)(void);
    void    (*settings_rom_path_set)(const char *path);
    int32_t (*settings_volume_get)(void);
    void    (*settings_volume_set)(int32_t level);
    int32_t (*settings_brightness_get)(void);
    void    (*settings_brightness_set)(int32_t level);

    /* ── FreeRTOS Tasks ──────────────────────────────────────────────── */
    int  (*task_create)(void (*fn)(void *), const char *name,
                        uint32_t stack_depth, void *arg,
                        int priority, void *out_handle, int core);
    void (*task_delete)(void *handle);

    /* ── PNG Loading ─────────────────────────────────────────────────── */
    uint16_t *(*png_load_rgb565)(const char *path,
                                  uint16_t *out_w, uint16_t *out_h);

    /* ── PPA Sprite Blit (hardware color-keyed blend) ────────────────── */
    int (*sprite_blit)(uint16_t *framebuf, uint32_t fb_w, uint32_t fb_h,
                       uint32_t x, uint32_t y,
                       const uint16_t *sprite, uint32_t sp_w, uint32_t sp_h,
                       uint16_t colorkey);

    /* ── PPA Framebuffer Copy (hardware DMA copy via PPA SRM) ────────── */
    int (*fb_copy)(const uint16_t *src, uint16_t *dst,
                   uint32_t w, uint32_t h);

} app_services_t;

/* ── Entry Point Signature ───────────────────────────────────────────── */
/*
 * Every PSRAM app must export this function at the entry_off specified
 * in its .papp header. It receives the service table and returns 0 on
 * success or a negative error code.
 */
typedef int (*papp_entry_fn_t)(const app_services_t *svc);

/* ── Loader API (launcher-side only) ─────────────────────────────────── */
#ifndef PAPP_APP_SIDE  /* Excluded when building PSRAM apps */

#include "esp_err.h"

/* Opaque handle for a loaded PSRAM app */
typedef struct psram_app *psram_app_handle_t;

/**
 * Load a .papp binary from the filesystem into PSRAM.
 * Does NOT execute it yet.
 *
 * @param path   Absolute path, e.g. "/sd/apps/opentyrian.papp"
 * @param handle Receives the loaded app handle
 * @return ESP_OK on success
 */
esp_err_t psram_app_load(const char *path, psram_app_handle_t *handle);

/**
 * Execute a previously loaded PSRAM app.
 * Maps code as executable, calls the entry point, waits for return,
 * then unmaps. Blocks until the app returns.
 *
 * @param handle  Loaded app handle
 * @return The app's return code (0 = success)
 */
int psram_app_run(psram_app_handle_t handle);

/**
 * Unload a PSRAM app and free all resources.
 *
 * @param handle  Loaded app handle (NULL-safe)
 */
void psram_app_unload(psram_app_handle_t handle);

/**
 * Self-test: verify PSRAM XIP works by loading and executing a tiny
 * built-in function from PSRAM. Logs results.
 *
 * @return ESP_OK if PSRAM code execution works
 */
esp_err_t psram_app_selftest(void);

#endif /* !PAPP_APP_SIDE */

#ifdef __cplusplus
}
#endif
