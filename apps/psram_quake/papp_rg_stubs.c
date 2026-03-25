/*
 * Retro-go API stubs for PSRAM Quake app.
 *
 * The Quake engine (compiled with -DESP32_QUAKE) references various rg_*
 * functions. We provide minimal stubs that route through app_services_t
 * or are no-ops.
 */
#include "psram_app.h"
#include <rg_system.h>
#include <string.h>
#include <stdarg.h>

extern const app_services_t *_papp_svc;
extern volatile int papp_exit_requested;
extern void app_return_to_launcher(void);

/* ── rg_system ───────────────────────────────────────────────────────── */

static rg_app_t s_app = { .romPath = "/sd/roms/quake/id1/pak0.pak" };

rg_app_t *rg_system_init(const rg_config_t *config)
{
    (void)config;
    return &s_app;
}

void rg_system_exit(void)
{
    app_return_to_launcher();
    for (;;) {}
}

int64_t rg_system_timer(void)
{
    return _papp_svc->get_time_us();
}

rg_app_t *rg_system_get_app(void)
{
    return &s_app;
}

int rg_system_get_app_speed(void)
{
    return 1;
}

void rg_system_tick(int64_t elapsed)
{
    (void)elapsed;
}

void rg_system_vlog(int level, const char *tag, const char *fmt, __builtin_va_list args)
{
    (void)level;
    (void)tag;
    if (_papp_svc && _papp_svc->log_vprintf)
        _papp_svc->log_vprintf(fmt, args);
}

/* ── rg_surface ──────────────────────────────────────────────────────── */

rg_surface_t *rg_surface_create(int width, int height, int format, uint32_t alloc_flags)
{
    rg_surface_t *s = (rg_surface_t *)_papp_svc->mem_calloc(1, sizeof(rg_surface_t));
    if (!s) return NULL;
    s->width = width;
    s->height = height;
    s->stride = width;
    s->format = format;
    s->data = _papp_svc->mem_caps_alloc(width * height, PAPP_MEM_CAP_SPIRAM);
    if (s->data)
        __builtin_memset(s->data, 0, width * height);
    if (format & RG_PIXEL_PALETTE)
        s->palette = (uint16_t *)_papp_svc->mem_calloc(256, sizeof(uint16_t));
    s->free_data = true;
    s->free_palette = true;
    return s;
}

void rg_surface_free(rg_surface_t *surface)
{
    if (!surface) return;
    if (surface->free_data && surface->data)
        _papp_svc->mem_free(surface->data);
    if (surface->free_palette && surface->palette)
        _papp_svc->mem_free(surface->palette);
    _papp_svc->mem_free(surface);
}

bool rg_surface_save_image_file(rg_surface_t *s, const char *fn, int w, int h)
{
    return false;
}

/* ── rg_display ──────────────────────────────────────────────────────── */

void rg_display_submit(rg_surface_t *update, int flags)
{
    /* VID_Update in papp_vid.c handles display — this is a no-op */
    (void)update; (void)flags;
}

bool rg_display_is_busy(void)
{
    return false;
}

int rg_display_get_width(void)
{
    return 320;
}

int rg_display_get_height(void)
{
    return 240;
}

void rg_display_set_scaling(int mode)
{
    (void)mode;
}

/* ── rg_input ────────────────────────────────────────────────────────── */

uint32_t rg_input_read_gamepad(void)
{
    papp_gamepad_state_t state;
    _papp_svc->input_gamepad_read(&state);

    uint32_t mask = 0;
    if (state.values[PAPP_INPUT_UP])     mask |= RG_KEY_UP;
    if (state.values[PAPP_INPUT_RIGHT])  mask |= RG_KEY_RIGHT;
    if (state.values[PAPP_INPUT_DOWN])   mask |= RG_KEY_DOWN;
    if (state.values[PAPP_INPUT_LEFT])   mask |= RG_KEY_LEFT;
    if (state.values[PAPP_INPUT_SELECT]) mask |= RG_KEY_SELECT;
    if (state.values[PAPP_INPUT_START])  mask |= RG_KEY_START;
    if (state.values[PAPP_INPUT_A])      mask |= RG_KEY_A;
    if (state.values[PAPP_INPUT_B])      mask |= RG_KEY_B;
    if (state.values[PAPP_INPUT_X])      mask |= RG_KEY_X;
    if (state.values[PAPP_INPUT_Y])      mask |= RG_KEY_Y;
    if (state.values[PAPP_INPUT_L])      mask |= RG_KEY_L;
    if (state.values[PAPP_INPUT_R])      mask |= RG_KEY_R;
    if (state.values[PAPP_INPUT_MENU])   mask |= RG_KEY_MENU;
    return mask;
}

/* ── rg_audio ────────────────────────────────────────────────────────── */

typedef struct { int64_t totalSamples; } rg_audio_counters_t;

void rg_audio_submit(rg_audio_sample_t *buf, size_t count)
{
    _papp_svc->audio_submit((short *)buf, (int)count);
}

void rg_audio_set_mute(bool mute)
{
    (void)mute;
}

int rg_audio_get_sample_rate(void)
{
    return 11025;
}

rg_audio_counters_t rg_audio_get_counters(void)
{
    rg_audio_counters_t c = { .totalSamples = 0 };
    return c;
}

/* ── rg_task ─────────────────────────────────────────────────────────── */

void rg_task_create(const char *name, void (*fn)(void*), void *arg,
                    int stack, int affinity, int priority, int core)
{
    void *handle = NULL;
    _papp_svc->task_create(fn, name, stack > 2048 ? stack : 4096, arg,
                           priority, &handle, core);
}

void rg_task_yield(void)
{
    _papp_svc->delay_ms(1);
}

void rg_task_delay(int ms)
{
    _papp_svc->delay_ms(ms);
}

void rg_usleep(unsigned long us)
{
    int ms = (int)(us / 1000);
    if (ms < 1) ms = 1;
    _papp_svc->delay_ms(ms);
}

/* ── rg_settings ─────────────────────────────────────────────────────── */

int32_t rg_settings_get_number(const char *ns, const char *key, int32_t def)
{
    (void)ns; (void)key;
    return def;
}

void rg_settings_set_number(const char *ns, const char *key, int32_t val)
{
    (void)ns; (void)key; (void)val;
}

/* ── rg_gui ──────────────────────────────────────────────────────────── */

void rg_gui_options_menu(void) {}
void rg_gui_game_menu(void) {}
void rg_gui_alert(const char *title, const char *msg)
{
    if (_papp_svc)
        _papp_svc->log_printf("QUAKE GUI: %s — %s\n",
                              title ? title : "", msg ? msg : "");
}
void rg_gui_draw_hourglass(void) {}

const char *rg_gui_file_picker(const char *title, const char *path,
                                bool (*filter)(const char*), bool a, bool b)
{
    (void)title; (void)path; (void)filter; (void)a; (void)b;
    return NULL;
}

/* ── rg_storage ──────────────────────────────────────────────────────── */

bool rg_extension_match(const char *path, const char *ext)
{
    if (!path || !ext) return false;
    size_t plen = strlen(path);
    size_t elen = strlen(ext);
    if (plen < elen + 1) return false;
    return (path[plen - elen - 1] == '.' &&
            __builtin_memcmp(path + plen - elen, ext, elen) == 0);
}

bool rg_storage_read_file(const char *path, void **data, size_t *len, int flags)
{
    (void)path; (void)data; (void)len; (void)flags;
    return false;
}

bool rg_storage_unzip_file(const char *path, const char *entry,
                            void **data, size_t *len, int flags)
{
    (void)path; (void)entry; (void)data; (void)len; (void)flags;
    return false;
}

bool rg_storage_mkdir(const char *path)
{
    (void)path;
    return true;
}
