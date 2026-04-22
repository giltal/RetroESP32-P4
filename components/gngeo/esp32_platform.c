/*
 * esp32_platform.c — ESP32-P4 platform stubs for GnGeo
 *
 * Provides all functions that were originally in:
 *   screen.c, sound.c, event.c, menu.c, conf.c, messages.c, debug.c
 * These are replaced with ESP32 equivalents or no-op stubs.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdbool.h>
#include "SDL.h"
#include "emu.h"
#include "screen.h"
#include "conf.h"
#include "sound.h"
#include "event.h"
#include "menu.h"
#include "messages.h"
#include "video.h"
#include "memory.h"
#include "state.h"
#include "roms.h"
#include "debug.h"
#include "frame_skip.h"

#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "odroid_display.h"
#include "odroid_input.h"
#include "odroid_audio.h"

/* stb_zlib for uncompress() implementation */
#include "stb_zlib.h"

static const char *TAG = "gngeo_esp32";

/* ──────────────────────────────────────────────────────
 * Screen globals (declared in screen.h as non-extern!)
 * ────────────────────────────────────────────────────── */
static SDL_PixelFormat screen_fmt = { .BitsPerPixel = 16, .BytesPerPixel = 2,
    .Rmask = 0xF800, .Gmask = 0x07E0, .Bmask = 0x001F, .Amask = 0 };
static SDL_PixelFormat buf_fmt = { .BitsPerPixel = 16, .BytesPerPixel = 2,
    .Rmask = 0xF800, .Gmask = 0x07E0, .Bmask = 0x001F, .Amask = 0 };

/* The real framebuffers — allocated in screen_init */
static SDL_Surface screen_surface;
static SDL_Surface buffer_surface;
static SDL_Surface sprbuf_surface;

/* Global pointers (screen.h now uses extern) */
SDL_Surface *screen = NULL;
SDL_Surface *buffer = NULL;
SDL_Surface *sprbuf = NULL;
SDL_Surface *fps_buf = NULL;
SDL_Surface *scan = NULL;
SDL_Surface *fontbuf = NULL;
SDL_Rect visible_area;

int yscreenpadding = 0;
Uint8 interpolation = 0;
Uint8 nblitter = 0;
Uint8 neffect = 0;
Uint8 scale = 1;
Uint8 fullscreen = 0;

/* ──────────────────────────────────────────────────────
 * Video globals referenced by emu.c / video.c
 * (only define those NOT already in video.c)
 * ────────────────────────────────────────────────────── */

/* Pending save/load state slots */
int pending_save_state = 0;
int pending_load_state = 0;
int show_fps = 0;
int autoframeskip = 0;
int sleep_idle = 0;
int show_keysym = 0;
char input_buf[256];

/* emu.h globals */
Uint8 key[SDLK_LAST];
Uint8 *joy_button[2];
Sint32 *joy_axe[2];
Uint32 joy_numaxes[2];

/* state_img for menu screenshot capture */
static SDL_Surface state_img_surface;
SDL_Surface *state_img = &state_img_surface;
Uint8 state_version = 3;
char fps_str[32];
struct gngeo_conf conf;

/* ──────────────────────────────────────────────────────
 * zlib uncompress via stb_zlib
 * ────────────────────────────────────────────────────── */
int uncompress(uint8_t *dest, unsigned long *destLen,
               const uint8_t *source, unsigned long sourceLen) {
    zbuf z;
    memset(&z, 0, sizeof(z));
    z.zbuffer = (uint8 *)source;
    z.zbuffer_end = (uint8 *)source + sourceLen;
    int result = stbi_zlib_decode_noheader_stream(&z, (char *)dest, (int)*destLen);
    if (result < 0) return -1;
    *destLen = result;
    return 0;
}

/* ──────────────────────────────────────────────────────
 * Screen functions
 * ────────────────────────────────────────────────────── */
#define NEO_SCREEN_W 320
#define NEO_SCREEN_H 224

static uint16_t *screen_pixels;
static uint16_t *buffer_pixels;
static uint16_t *sprbuf_pixels;
static uint16_t *lcd_fb;  /* contiguous visible-area buffer for LCD blit */

int screen_init(void) {
    ESP_LOGI(TAG, "screen_init: %dx%d RGB565", NEO_SCREEN_W + 32, NEO_SCREEN_H + 32);

    /* Allocate in PSRAM for large buffers */
    screen_pixels = heap_caps_calloc(1, (NEO_SCREEN_W + 32) * (NEO_SCREEN_H + 32) * 2, MALLOC_CAP_SPIRAM);
    buffer_pixels = heap_caps_calloc(1, (NEO_SCREEN_W + 32) * (NEO_SCREEN_H + 32) * 2, MALLOC_CAP_SPIRAM);
    sprbuf_pixels = heap_caps_calloc(1, (NEO_SCREEN_W + 32) * (NEO_SCREEN_H + 32) * 2, MALLOC_CAP_SPIRAM);

    if (!screen_pixels || !buffer_pixels || !sprbuf_pixels) {
        ESP_LOGE(TAG, "Failed to allocate screen buffers");
        return -1;
    }

    /* Set up SDL_Surface wrappers */
    memset(&screen_surface, 0, sizeof(screen_surface));
    screen_surface.format = &screen_fmt;
    screen_surface.w = NEO_SCREEN_W + 32;
    screen_surface.h = NEO_SCREEN_H + 32;
    screen_surface.pitch = (NEO_SCREEN_W + 32) * 2;
    screen_surface.pixels = screen_pixels;
    screen = &screen_surface;

    memset(&buffer_surface, 0, sizeof(buffer_surface));
    buffer_surface.format = &buf_fmt;
    buffer_surface.w = NEO_SCREEN_W + 32;
    buffer_surface.h = NEO_SCREEN_H + 32;
    buffer_surface.pitch = (NEO_SCREEN_W + 32) * 2;
    buffer_surface.pixels = buffer_pixels;
    buffer = &buffer_surface;

    memset(&sprbuf_surface, 0, sizeof(sprbuf_surface));
    sprbuf_surface.format = &buf_fmt;
    sprbuf_surface.w = NEO_SCREEN_W + 32;
    sprbuf_surface.h = NEO_SCREEN_H + 32;
    sprbuf_surface.pitch = (NEO_SCREEN_W + 32) * 2;
    sprbuf_surface.pixels = sprbuf_pixels;
    sprbuf = &sprbuf_surface;

    /* Visible area — standard Neo Geo viewport */
    visible_area.x = 16;
    visible_area.y = 16;
    visible_area.w = 304;
    visible_area.h = 224;

    /* fps_buf, scan, fontbuf — not needed, set to NULL */
    fps_buf = NULL;
    scan = NULL;
    fontbuf = NULL;

    /* Allocate contiguous buffer for visible area → LCD blit */
    lcd_fb = heap_caps_calloc(1, 304 * 224 * 2, MALLOC_CAP_SPIRAM);
    if (!lcd_fb) {
        ESP_LOGE(TAG, "Failed to allocate LCD framebuffer");
        return -1;
    }

    return 0;
}

int screen_reinit(void) {
    if (!screen_pixels) return screen_init();
    return 0;
}

int screen_resize(int w, int h) { (void)w; (void)h; return 0; }

void screen_update(void) {
    if (!buffer_pixels || !lcd_fb) return;

    /* Extract visible area (304x224) from buffer (352 pixels wide, offset 16,16) */
    const int src_stride = NEO_SCREEN_W + 32; /* 352 */
    const int vis_w = visible_area.w;          /* 304 */
    const int vis_h = visible_area.h;          /* 224 */
    const int ox = visible_area.x;             /* 16  */
    const int oy = visible_area.y;             /* 16  */

    for (int y = 0; y < vis_h; y++) {
        memcpy(&lcd_fb[y * vis_w],
               &buffer_pixels[(oy + y) * src_stride + ox],
               vis_w * sizeof(uint16_t));
    }

    /* Push to LCD — PPA will scale + rotate */
    ili9341_write_frame_rgb565_custom(lcd_fb, vis_w, vis_h, 2.0f, false);
}

void screen_close(void) {
    if (screen_pixels) { heap_caps_free(screen_pixels); screen_pixels = NULL; }
    if (buffer_pixels) { heap_caps_free(buffer_pixels); buffer_pixels = NULL; }
    if (sprbuf_pixels) { heap_caps_free(sprbuf_pixels); sprbuf_pixels = NULL; }
    if (lcd_fb)        { heap_caps_free(lcd_fb);        lcd_fb = NULL; }
}
void screen_fullscreen(void) {}
void init_sdl(void) { screen_init(); }

/* ──────────────────────────────────────────────────────
 * Sound implementation — Z80 + YM2610 on core 1, audio async
 * ────────────────────────────────────────────────────── */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "timer.h"
#include "esp_timer.h"

static SDL_AudioSpec desired_spec, obtain_spec;
SDL_AudioSpec *desired = &desired_spec;
SDL_AudioSpec *obtain = &obtain_spec;

/* Double-buffered play_buffer: YM2610 renders into one while I2S pushes the other */
Uint16 *play_buffer = NULL;          /* points to active render buffer */
static Uint16 *play_buf_a = NULL;
static Uint16 *play_buf_b = NULL;
static Uint16 *submit_buf = NULL;    /* buffer being submitted by audio task */
static int submit_len = 0;           /* frame count to submit */

/* Number of stereo samples to generate per frame (sample_rate / 60) */
static int audio_samples_per_frame = 367;

/* ── Z80 task on core 1 ── */
/* Message types for the Z80 command queue */
typedef enum {
    Z80_CMD_NMI,          /* 68K sent sound command → fire NMI + run 300 cycles */
    Z80_CMD_FRAME,        /* Run the per-frame Z80 loop + audio synthesis */
    Z80_CMD_STOP,         /* Shutdown */
} z80_cmd_t;

static QueueHandle_t z80_cmd_queue = NULL;
static SemaphoreHandle_t z80_frame_done_sem = NULL;
static volatile bool z80_task_running = false;

/* Z80 timeslice parameters (set during init) */
static int z80_nb_interlace = 16;
static Uint32 z80_timeslice_interlace = 0;

/* Audio I2S submission synchronization */
static SemaphoreHandle_t audio_ready_sem = NULL;
static SemaphoreHandle_t audio_done_sem = NULL;
static volatile bool audio_task_running = false;

static void audio_submit_task(void *arg) {
    (void)arg;
    while (audio_task_running) {
        if (xSemaphoreTake(audio_ready_sem, pdMS_TO_TICKS(100)) == pdTRUE) {
            if (submit_buf && submit_len > 0) {
                odroid_audio_submit((short *)submit_buf, submit_len);
            }
            xSemaphoreGive(audio_done_sem);
        }
    }
    vTaskDelete(NULL);
}

static void z80_sound_task(void *arg) {
    (void)arg;
    extern void cpu_z80_run(int nbcycle);
    extern void cpu_z80_nmi(void);
    extern void YM2610Update_stream(int length);

    z80_cmd_t cmd;
    int64_t last_stat = 0;
    int64_t sum_z80 = 0, sum_ym = 0;
    int z80_stat_count = 0;

    while (z80_task_running) {
        if (xQueueReceive(z80_cmd_queue, &cmd, pdMS_TO_TICKS(100)) == pdTRUE) {
            switch (cmd) {
            case Z80_CMD_NMI:
                /* Process sound command from 68K */
                cpu_z80_nmi();
                cpu_z80_run(300);
                break;

            case Z80_CMD_FRAME: {
                /* Drain any pending NMI commands first */
                z80_cmd_t pending;
                while (xQueueReceive(z80_cmd_queue, &pending, 0) == pdTRUE) {
                    if (pending == Z80_CMD_NMI) {
                        cpu_z80_nmi();
                        cpu_z80_run(300);
                    } else if (pending == Z80_CMD_STOP) {
                        z80_task_running = false;
                        goto z80_exit;
                    }
                }

                /* Run Z80 for one frame */
                int64_t tz80_s = esp_timer_get_time();
                for (int i = 0; i < z80_nb_interlace; i++) {
                    cpu_z80_run(z80_timeslice_interlace);
                    my_timer();
                }
                int64_t tz80_e = esp_timer_get_time();

                /* Synthesize audio and hand off to I2S task */
                xSemaphoreTake(audio_done_sem, pdMS_TO_TICKS(20));
                YM2610Update_stream(audio_samples_per_frame);
                int64_t tym_e = esp_timer_get_time();
                submit_buf = play_buffer;
                submit_len = audio_samples_per_frame;
                play_buffer = (play_buffer == play_buf_a) ? play_buf_b : play_buf_a;
                xSemaphoreGive(audio_ready_sem);

                /* Z80 task timing stats */
                sum_z80 += (tz80_e - tz80_s);
                sum_ym += (tym_e - tz80_e);
                z80_stat_count++;
                if (tym_e - last_stat > 1000000) {
                    printf("  Z80task: z80=%lld us, ym=%lld us (n=%d)\n",
                           sum_z80 / z80_stat_count, sum_ym / z80_stat_count, z80_stat_count);
                    sum_z80 = 0; sum_ym = 0; z80_stat_count = 0;
                    last_stat = tym_e;
                }

                /* Signal emu loop that Z80 frame is done */
                xSemaphoreGive(z80_frame_done_sem);
                break;
            }

            case Z80_CMD_STOP:
                z80_task_running = false;
                break;
            }
        }
    }
z80_exit:
    xSemaphoreGive(z80_frame_done_sem); /* unblock any waiter */
    vTaskDelete(NULL);
}

int init_sdl_audio(void) {
    ESP_LOGI(TAG, "init_sdl_audio: initializing ES8311 at %d Hz", conf.sample_rate);

    audio_samples_per_frame = conf.sample_rate / 60;

    /* Allocate double play_buffers in PSRAM */
    if (!play_buf_a) {
        play_buf_a = heap_caps_calloc(16384, sizeof(Uint16), MALLOC_CAP_SPIRAM);
        play_buf_b = heap_caps_calloc(16384, sizeof(Uint16), MALLOC_CAP_SPIRAM);
        if (!play_buf_a || !play_buf_b) {
            ESP_LOGE(TAG, "Failed to allocate play_buffers in PSRAM");
            return -1;
        }
        play_buffer = play_buf_a;
    }

    desired->freq = conf.sample_rate;
    desired->format = AUDIO_S16;
    desired->channels = 2;
    desired->samples = audio_samples_per_frame;
    desired->size = audio_samples_per_frame * 2 * sizeof(int16_t);
    desired->callback = NULL;
    desired->userdata = NULL;

    memcpy(obtain, desired, sizeof(SDL_AudioSpec));

    odroid_audio_init(conf.sample_rate);
    /* Set volume to ~50% for comfortable debugging */
    odroid_audio_volume_set(2); /* Level 2 of 4 = 50% */

    /* Compute Z80 timeslice */
    int z80_overclk = CF_VAL(cf_get_item_by_name("z80clock"));
    Uint32 z80_ts = (z80_overclk == 0 ? 73333 : 73333 + (z80_overclk * 73333 / 100));
    z80_timeslice_interlace = z80_ts / (float)z80_nb_interlace;

    /* Create command queue (depth 16: enough for NMI bursts + frame cmd) */
    z80_cmd_queue = xQueueCreate(16, sizeof(z80_cmd_t));
    z80_frame_done_sem = xSemaphoreCreateBinary();

    /* Create audio I2S submission task */
    audio_ready_sem = xSemaphoreCreateBinary();
    audio_done_sem = xSemaphoreCreateBinary();
    xSemaphoreGive(audio_done_sem);
    audio_task_running = true;

    static StaticTask_t audio_tcb;
    static StackType_t audio_stack[2048];
    xTaskCreateStaticPinnedToCore(audio_submit_task, "neo_audio", 2048,
                                  NULL, 6, audio_stack, &audio_tcb, 1);

    /* Create Z80 sound task on core 1 */
    z80_task_running = true;
    static StaticTask_t z80_tcb;
    static StackType_t z80_stack[4096];
    xTaskCreateStaticPinnedToCore(z80_sound_task, "neo_z80", 4096,
                                  NULL, 7, z80_stack, &z80_tcb, 1);

    ESP_LOGI(TAG, "Audio ready: %d Hz, %d samples/frame, Z80 on core 1",
             conf.sample_rate, audio_samples_per_frame);
    return 0;
}

void close_sdl_audio(void) {
    if (z80_task_running && z80_cmd_queue) {
        z80_cmd_t cmd = Z80_CMD_STOP;
        xQueueSend(z80_cmd_queue, &cmd, pdMS_TO_TICKS(100));
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    z80_task_running = false;
    audio_task_running = false;
    if (audio_ready_sem) xSemaphoreGive(audio_ready_sem);
    vTaskDelay(pdMS_TO_TICKS(150));
    odroid_audio_terminate();
}

void pause_audio(int on) {
    (void)on;
}

/* Called from 68K store handler: queue an NMI for the Z80 task */
void esp32_z80_queue_nmi(void) {
    if (z80_cmd_queue) {
        z80_cmd_t cmd = Z80_CMD_NMI;
        xQueueSend(z80_cmd_queue, &cmd, 0); /* non-blocking */
    }
}

/* Called from emu loop: kick off Z80 frame on core 1 */
void esp32_z80_start_frame(void) {
    if (z80_cmd_queue) {
        z80_cmd_t cmd = Z80_CMD_FRAME;
        xQueueSend(z80_cmd_queue, &cmd, portMAX_DELAY);
    }
}

/* Called from emu loop: wait for Z80 frame to complete */
void esp32_z80_wait_frame(void) {
    if (z80_frame_done_sem) {
        xSemaphoreTake(z80_frame_done_sem, pdMS_TO_TICKS(50));
    }
}

/* Legacy stub — no longer called from emu loop */
void esp32_audio_submit_frame(void) {
    /* Now handled inside z80_sound_task */
}

/* ──────────────────────────────────────────────────────
 * Event / Input
 * ────────────────────────────────────────────────────── */
int init_event(void) { return 0; }
void reset_event(void) {}
int wait_event(void) { return 0; }

/* Event globals */
JOYMAP *jmap = NULL;
Uint8 joy_state[2][GN_MAX_KEY];
static int menu_quit_requested = 0;

static uint16_t prev_gp_bits = 0;  /* track previous raw gamepad state for change detection */

int handle_event(void) {
    /* Read gamepad once per frame */
    odroid_gamepad_state gp;
    odroid_input_gamepad_read(&gp);

    /* Build a bitmask of all pressed buttons for change detection */
    uint16_t gp_bits = 0;
    if (gp.values[ODROID_INPUT_UP])     gp_bits |= (1 << 0);
    if (gp.values[ODROID_INPUT_DOWN])   gp_bits |= (1 << 1);
    if (gp.values[ODROID_INPUT_LEFT])   gp_bits |= (1 << 2);
    if (gp.values[ODROID_INPUT_RIGHT])  gp_bits |= (1 << 3);
    if (gp.values[ODROID_INPUT_A])      gp_bits |= (1 << 4);
    if (gp.values[ODROID_INPUT_B])      gp_bits |= (1 << 5);
    if (gp.values[ODROID_INPUT_X])      gp_bits |= (1 << 6);
    if (gp.values[ODROID_INPUT_Y])      gp_bits |= (1 << 7);
    if (gp.values[ODROID_INPUT_START])  gp_bits |= (1 << 8);
    if (gp.values[ODROID_INPUT_SELECT]) gp_bits |= (1 << 9);
    if (gp.values[ODROID_INPUT_L])      gp_bits |= (1 << 10);
    if (gp.values[ODROID_INPUT_R])      gp_bits |= (1 << 11);

    /* Log on any change */
    if (gp_bits != prev_gp_bits) {
        printf("GP_RAW: bits=%04x (U=%d D=%d L=%d R=%d A=%d B=%d X=%d Y=%d ST=%d SEL=%d L=%d R=%d)\n",
               gp_bits,
               gp.values[ODROID_INPUT_UP], gp.values[ODROID_INPUT_DOWN],
               gp.values[ODROID_INPUT_LEFT], gp.values[ODROID_INPUT_RIGHT],
               gp.values[ODROID_INPUT_A], gp.values[ODROID_INPUT_B],
               gp.values[ODROID_INPUT_X], gp.values[ODROID_INPUT_Y],
               gp.values[ODROID_INPUT_START], gp.values[ODROID_INPUT_SELECT],
               gp.values[ODROID_INPUT_L], gp.values[ODROID_INPUT_R]);
        prev_gp_bits = gp_bits;
    }

    /* Map to joy_state for P1 */
    joy_state[0][GN_UP]          = gp.values[ODROID_INPUT_UP];
    joy_state[0][GN_DOWN]        = gp.values[ODROID_INPUT_DOWN];
    joy_state[0][GN_LEFT]        = gp.values[ODROID_INPUT_LEFT];
    joy_state[0][GN_RIGHT]       = gp.values[ODROID_INPUT_RIGHT];
    joy_state[0][GN_A]           = gp.values[ODROID_INPUT_A];
    joy_state[0][GN_B]           = gp.values[ODROID_INPUT_B];
    joy_state[0][GN_C]           = gp.values[ODROID_INPUT_X];
    joy_state[0][GN_D]           = gp.values[ODROID_INPUT_Y];
    joy_state[0][GN_START]       = gp.values[ODROID_INPUT_START];
    joy_state[0][GN_SELECT_COIN] = gp.values[ODROID_INPUT_SELECT];

    /* MENU: touch left shoulder = return to launcher */
    if (gp.values[ODROID_INPUT_MENU]) {
        menu_quit_requested = 1;
        return 1; /* non-zero = open menu */
    }

    return 0;
}

/*
 * Neo Geo controller registers (active-low: 0 = pressed, 1 = released)
 * intern_p1/p2:  bit0=UP, 1=DOWN, 2=LEFT, 3=RIGHT, 4=A, 5=B, 6=C, 7=D
 * intern_start:  bit0=P1 START, bit2=P2 START, bit3=P1 SELECT, upper=0x8F idle
 * intern_coin:   bit0=COIN1, bit1=COIN2, bit2=SERVICE, 0x07 idle
 */
void update_p1_key(void) {
    Uint8 val = 0xFF;
    if (joy_state[0][GN_UP])    val &= ~(1 << 0);
    if (joy_state[0][GN_DOWN])  val &= ~(1 << 1);
    if (joy_state[0][GN_LEFT])  val &= ~(1 << 2);
    if (joy_state[0][GN_RIGHT]) val &= ~(1 << 3);
    if (joy_state[0][GN_A])     val &= ~(1 << 4);
    if (joy_state[0][GN_B])     val &= ~(1 << 5);
    if (joy_state[0][GN_C])     val &= ~(1 << 6);
    if (joy_state[0][GN_D])     val &= ~(1 << 7);
    memory.intern_p1 = val;
}

void update_p2_key(void) {
    memory.intern_p2 = 0xFF;
}

void update_start(void) {
    Uint8 val = 0xFF;  /* All idle: bits 0-3=buttons, bit4-5=memcard prot, bit6=no card, bit7=1 */
    if (joy_state[0][GN_START])       val &= ~(1 << 0); /* P1 START */
    memory.intern_start = val;
}

void update_coin(void) {
    int coin_pressed = joy_state[0][GN_SELECT_COIN];
    Uint8 val = 0x07;
    if (coin_pressed) val &= ~(1 << 0); /* COIN 1 */
    memory.intern_coin = val;
}

/* ──────────────────────────────────────────────────────
 * Menu stubs
 * ────────────────────────────────────────────────────── */

Uint32 run_menu(void) {
    if (menu_quit_requested) {
        menu_quit_requested = 0;
        return 2; /* 2 = quit */
    }
    return 0; /* 0 = resume */
}
int gn_init_skin(void) { return 0; }
void gn_reset_pbar(void) {}
void gn_init_pbar(char *name, int size) {
    ESP_LOGI(TAG, "Loading: %s (size=%d)", name ? name : "?", size);
}
void gn_update_pbar(int pos) { (void)pos; }
void gn_terminate_pbar(void) {
    ESP_LOGI(TAG, "Loading complete");
}
void gn_popup_error(char *name, char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    printf("[ERROR] %s: ", name ? name : "");
    vprintf(fmt, ap);
    printf("\n");
    va_end(ap);
}
int gn_popup_question(char *name, char *fmt, ...) {
    (void)name; (void)fmt;
    return 0;
}

/* ──────────────────────────────────────────────────────
 * Conf (configuration) stubs
 * ────────────────────────────────────────────────────── */

/* Static config items used by the emulator */
static CONF_ITEM cf_rompath   = { .name = "rompath",   .type = CFT_STRING, .data.dt_str = { .str = "/sd/roms/neogeo" } };
static CONF_ITEM cf_biospath  = { .name = "biospath",  .type = CFT_STRING, .data.dt_str = { .str = "/sd/roms/neogeo" } };
static CONF_ITEM cf_blitter   = { .name = "blitter",   .type = CFT_STRING, .data.dt_str = { .str = "soft" } };
static CONF_ITEM cf_effect    = { .name = "effect",    .type = CFT_STRING, .data.dt_str = { .str = "none" } };
static CONF_ITEM cf_transpack = { .name = "transpack", .type = CFT_STRING, .data.dt_str = { .str = "" } };
static CONF_ITEM cf_system    = { .name = "system",    .type = CFT_STRING, .data.dt_str = { .str = "arcade" } };
static CONF_ITEM cf_country   = { .name = "country",   .type = CFT_STRING, .data.dt_str = { .str = "usa" } };
static CONF_ITEM cf_68kclock  = { .name = "68kclock",  .type = CFT_INT,    .data.dt_int = { .val = 0 } };
static CONF_ITEM cf_z80clock  = { .name = "z80clock",  .type = CFT_INT,    .data.dt_int = { .val = 0 } };
static CONF_ITEM cf_debug     = { .name = "debug",     .type = CFT_BOOLEAN,.data.dt_bool = { .boolean = 0 } };
static CONF_ITEM cf_raster    = { .name = "raster",    .type = CFT_BOOLEAN,.data.dt_bool = { .boolean = 0 } };
static CONF_ITEM cf_sound     = { .name = "sound",     .type = CFT_BOOLEAN,.data.dt_bool = { .boolean = 1 } };
static CONF_ITEM cf_interp    = { .name = "interpolation", .type = CFT_INT,.data.dt_int = { .val = 0 } };
static CONF_ITEM cf_scale     = { .name = "scale",     .type = CFT_INT,    .data.dt_int = { .val = 1 } };
static CONF_ITEM cf_fullscreen = { .name = "fullscreen", .type = CFT_BOOLEAN, .data.dt_bool = { .boolean = 0 } };
static CONF_ITEM cf_autoframeskip = { .name = "autoframeskip", .type = CFT_BOOLEAN, .data.dt_bool = { .boolean = 1 } };
static CONF_ITEM cf_showfps   = { .name = "showfps",   .type = CFT_BOOLEAN,.data.dt_bool = { .boolean = 0 } };
static CONF_ITEM cf_sleepidle = { .name = "sleepidle", .type = CFT_BOOLEAN,.data.dt_bool = { .boolean = 0 } };
static CONF_ITEM cf_screen320 = { .name = "screen320", .type = CFT_BOOLEAN,.data.dt_bool = { .boolean = 0 } };
static CONF_ITEM cf_pal       = { .name = "pal",       .type = CFT_BOOLEAN,.data.dt_bool = { .boolean = 0 } };
static CONF_ITEM cf_sample_rate = { .name = "samplerate", .type = CFT_INT, .data.dt_int = { .val = 22050 } };
static CONF_ITEM cf_libglpath = { .name = "libglpath", .type = CFT_STRING, .data.dt_str = { .str = "" } };
static CONF_ITEM cf_datafile  = { .name = "datafile",  .type = CFT_STRING, .data.dt_str = { .str = "/sd/roms/neogeo/gngeo_data.zip" } };
static CONF_ITEM cf_bench     = { .name = "bench",     .type = CFT_BOOLEAN,.data.dt_bool = { .boolean = 0 } };
static CONF_ITEM cf_dump      = { .name = "dump",      .type = CFT_BOOLEAN,.data.dt_bool = { .boolean = 0 } };

/* Config item table */
static CONF_ITEM *cf_items[] = {
    &cf_rompath, &cf_biospath, &cf_blitter, &cf_effect, &cf_transpack,
    &cf_system, &cf_country, &cf_68kclock, &cf_z80clock, &cf_debug,
    &cf_raster, &cf_sound, &cf_interp, &cf_scale, &cf_fullscreen,
    &cf_autoframeskip, &cf_showfps, &cf_sleepidle, &cf_screen320,
    &cf_pal, &cf_sample_rate, &cf_libglpath, &cf_datafile,
    &cf_bench, &cf_dump,
    NULL
};

CONF_ITEM *cf_get_item_by_name(const char *name) {
    for (int i = 0; cf_items[i]; i++) {
        if (strcmp(cf_items[i]->name, name) == 0)
            return cf_items[i];
    }
    /* Return a dummy item to avoid NULL crashes */
    static CONF_ITEM cf_dummy = { .name = "dummy", .type = CFT_INT, .data.dt_int = { .val = 0 } };
    ESP_LOGW(TAG, "cf_get_item_by_name: '%s' not found", name);
    return &cf_dummy;
}

void cf_init(void) {}
void cf_reset_to_default(void) {}
int cf_open_file(char *filename) { (void)filename; return 0; }
int cf_save_file(char *filename, int flags) { (void)filename; (void)flags; return 0; }
int cf_save_option(char *filename, char *optname, int flags) { (void)filename; (void)optname; (void)flags; return 0; }

void cf_create_bool_item(const char *name, const char *help, char so, int def) { (void)name; (void)help; (void)so; (void)def; }
void cf_create_action_item(const char *name, const char *help, char so, int (*action)(struct CONF_ITEM *self)) { (void)name; (void)help; (void)so; (void)action; }
void cf_create_action_arg_item(const char *name, const char *help, const char *ha, char so, int (*action)(struct CONF_ITEM *self)) { (void)name; (void)help; (void)ha; (void)so; (void)action; }
void cf_create_string_item(const char *name, const char *help, const char *ha, char so, const char *def) { (void)name; (void)help; (void)ha; (void)so; (void)def; }
void cf_create_int_item(const char *name, const char *help, const char *ha, char so, int def) { (void)name; (void)help; (void)ha; (void)so; (void)def; }
void cf_create_array_item(const char *name, const char *help, const char *ha, char so, int size, int *def) { (void)name; (void)help; (void)ha; (void)so; (void)size; (void)def; }
void cf_create_str_array_item(const char *name, const char *help, const char *ha, char so, char *def) { (void)name; (void)help; (void)ha; (void)so; (void)def; }
void cf_init_cmd_line(void) {}
int cf_get_non_opt_index(int argc, char *argv[]) { (void)argc; (void)argv; return 0; }

/* ──────────────────────────────────────────────────────
 * Messages stubs
 * ────────────────────────────────────────────────────── */
void draw_message(const char *string) {
    ESP_LOGI(TAG, "MSG: %s", string ? string : "");
}
void stop_message(int param) { (void)param; }
void SDL_textout(SDL_Surface *dest, int x, int y, const char *string) {
    (void)dest; (void)x; (void)y; (void)string;
}
void text_input(const char *message, int x, int y, char *string, int size) {
    (void)message; (void)x; (void)y;
    if (string && size > 0) string[0] = '\0';
}
void error_box(char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
}

/* ──────────────────────────────────────────────────────
 * Debug stubs
 * ────────────────────────────────────────────────────── */
int dbg_step = 0;
void show_bt(void) {}
void add_bt(Uint32 pc) { (void)pc; }
int check_bp(int pc) { (void)pc; return 0; }
void add_bp(int pc) { (void)pc; }
void del_bp(int pc) { (void)pc; }
int dbg_68k_run(Uint32 nbcycle) { (void)nbcycle; return 0; }

/* ──────────────────────────────────────────────────────
 * RGB2YUV table (referenced by screen.h) — allocated in PSRAM to save internal RAM
 * ────────────────────────────────────────────────────── */
RGB2YUV *rgb2yuv = NULL;
void init_rgb2yuv_table(void) {
    if (!rgb2yuv) {
        rgb2yuv = heap_caps_calloc(65536, sizeof(RGB2YUV), MALLOC_CAP_SPIRAM);
    }
}

/* ──────────────────────────────────────────────────────
 * ESP32-specific: set ROM path for conf system
 * ────────────────────────────────────────────────────── */
void esp32_set_rompath(const char *path) {
    strncpy(cf_rompath.data.dt_str.str, path, CF_MAXSTRLEN - 1);
    strncpy(cf_biospath.data.dt_str.str, path, CF_MAXSTRLEN - 1);
}

void esp32_enable_sound(int enable) {
    cf_sound.data.dt_bool.boolean = enable;
    conf.sound = enable;
}

void esp32_init_conf(const char *game_name) {
    memset(&conf, 0, sizeof(conf));
    conf.game = (char *)game_name;
    conf.x_start = 16;
    conf.y_start = 16;
    conf.res_x = 304;
    conf.res_y = 224;
    conf.sample_rate = 22050;
    conf.sound = 1;      /* Sound enabled */
    conf.vsync = 0;
    conf.raster = 0;     /* Non-raster mode (simpler) */
    conf.debug = 0;
    conf.system = SYS_ARCADE;
    conf.country = CTY_USA;
    conf.autoframeskip = 1;
    conf.show_fps = 0;
    conf.sleep_idle = 0;
    conf.screen320 = 0;
    conf.pal = 0;
}
