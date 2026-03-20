/*
 * huexpress_run.c — Entry point for PC Engine / TurboGrafx-16 emulator on ESP32-P4
 *
 * Implements all OSD (Operating System Dependent) functions required by the
 * HuExpress engine, video output, audio mixing, gamepad input, and the main
 * emulation harness.
 *
 * The engine's RunPCE() is a blocking loop.  Video is submitted from the
 * osd_gfx_put_image_normal() callback; audio is mixed in a separate task.
 */

#include "huexpress_run.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>
#include <stdarg.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_heap_caps.h"
#include "esp_system.h"
#include "esp_timer.h"

/* HuExpress engine headers */
#include "pce.h"
#include "h6280.h"
#include "romdb.h"
#include "zipmgr.h"

/* Zip stubs — we only load uncompressed .pce ROMs from SD */
uint32 zipmgr_probe_file(char* zipFilename, char* foundGameFile) {
    (void)zipFilename; (void)foundGameFile;
    return ZIP_ERROR;
}
uint32 zipmgr_extract_to_disk(char* zipFilename, char* destination) {
    (void)zipFilename; (void)destination;
    return 0;
}
char* zipmgr_extract_to_memory(char* zipFilename, char* cartFilename, size_t* cartSize) {
    (void)zipFilename; (void)cartFilename; (void)cartSize;
    return NULL;
}

/* Odroid platform */
#include "odroid_settings.h"
#include "odroid_audio.h"
#include "odroid_input.h"
#include "odroid_system.h"
#include "odroid_display.h"
#include "odroid_sdcard.h"

/* ────────────────────────────────────────────────────────────────── */
/*  Constants                                                         */
/* ────────────────────────────────────────────────────────────────── */
#define AUDIO_SAMPLE_RATE   22050
#define AUDIO_BUFFER_SIZE   4096
#define AUDIO_CHANNELS      6
#define PCE_FB_WIDTH        320
#define PCE_FB_HEIGHT       240
#define PATH_MAX_MY         128

/* ────────────────────────────────────────────────────────────────── */
/*  OSD-required globals (referenced by engine via extern)            */
/* ────────────────────────────────────────────────────────────────── */

/* Framebuffer / video */
uint8_t  *framebuffer[2];
uint8_t   current_framebuffer = 0;
uchar    *XBuf;
uchar    *osd_gfx_buffer            = NULL;
uint16_t *my_palette                = NULL;
bool      skipNextFrame             = false;  /* never skip — always render */
QueueHandle_t vidQueue              = NULL;   /* not used, but declared extern by gfx.h */

/* extern from pce.c / pce.h — engine allocates some path buffers itself,
   but the ESP32 build allocates them in the init code: */
extern char *cart_name;
extern char *short_cart_name;
extern char *short_iso_name;
extern char *rom_file_name;
extern char *config_basepath;
extern char *sav_path;
extern char *sav_basepath;
extern char *tmp_basepath;
extern char *video_path;
extern char *ISO_filename;
extern char *syscard_filename;
extern char *cdsystem_path;
extern char *log_filename;
extern uint32 *spr_init_pos;
extern uchar *SPM_raw;
extern uchar *SPM;

/* Sound globals – the engine's sound.c #else branch provides gen_vol etc. */
extern uchar gen_vol;

/* ────────────────────────────────────────────────────────────────── */
/*  Audio task private data                                           */
/* ────────────────────────────────────────────────────────────────── */
char           *sbuf[AUDIO_CHANNELS];
static short       *sbuf_mix[2];
static QueueHandle_t audioQueue         = NULL;
static TaskHandle_t  audioTaskHandle    = NULL;
static volatile bool audioTaskIsRunning = false;
#define TASK_BREAK ((void*)1)

/* ────────────────────────────────────────────────────────────────── */
/*  Menu / input state                                                */
/* ────────────────────────────────────────────────────────────────── */
static bool     ignoreMenuButton       = true;
static uint16_t menuButtonFrameCount   = 0;
static odroid_gamepad_state previousJoystickState;
static volatile bool pce_quit_flag     = false;
static volatile bool pce_pending_load  = false;  /* deferred state load */

/* ────────────────────────────────────────────────────────────────── */
/*  FPS counter                                                       */
/* ────────────────────────────────────────────────────────────────── */
static int64_t fps_start_us;
static int     fps_frame_count;

/* ────────────────────────────────────────────────────────────────── */
/*  Memory allocator (called by engine)                                */
/* ────────────────────────────────────────────────────────────────── */
void *my_special_alloc(unsigned char speed, unsigned char bytes, unsigned long size)
{
    uint32_t caps;
    if (speed) {
        /* Try internal RAM first, fallback to PSRAM */
        caps = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
        void *p = heap_caps_malloc(size, caps);
        if (p) {
            printf("ALLOC: %lu bytes internal → %p\n", size, p);
            return p;
        }
    }
    /* PSRAM */
    caps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
    void *p = heap_caps_malloc(size, caps);
    printf("ALLOC: %lu bytes SPIRAM → %p\n", size, p);
    if (!p) abort();
    return p;
}

/* ────────────────────────────────────────────────────────────────── */
/*  OSD Graphics functions                                            */
/* ────────────────────────────────────────────────────────────────── */

/*
 * osd_gfx_set_color — engine calls this to build the 256-entry palette.
 * r/g/b are 6-bit values (0-63) from the TG16 VCE.
 * Convert to native-endian RGB565 (no byte swap — PPA expects native).
 */
void osd_gfx_set_color(uchar index, uchar r, uchar g, uchar b)
{
    /* Scale 6-bit (0-63) → 8-bit (0-255) by replicating top bits into bottom */
    uint8_t r8 = (r << 2) | (r >> 4);
    uint8_t g8 = (g << 2) | (g >> 4);
    uint8_t b8 = (b << 2) | (b >> 4);

    /* Standard 8-bit → native RGB565 (same as MAKE_PIXEL in smsplus) */
    my_palette[index] = ((r8 << 8) & 0xF800) | ((g8 << 3) & 0x07E0) | (b8 >> 3);
}

/* Forward declarations for the driver list */
static int  pce_gfx_init(void);
static int  pce_gfx_mode(void);
static void pce_gfx_draw(void);
static void pce_gfx_shut(void);

/* The osd_gfx_driver_list — engine calls .draw() per frame */
#include "sys_gfx.h"
osd_gfx_driver osd_gfx_driver_list[1] = {
    { pce_gfx_init, pce_gfx_mode, pce_gfx_draw, pce_gfx_shut }
};

static int pce_gfx_init(void)
{
    printf("PCE GFX: init\n");
    SetPalette();   /* build the 256-entry indexed → RGB565 lookup table */
    fps_start_us = esp_timer_get_time();
    fps_frame_count = 0;
    return 1;   /* true = success */
}

/* Called when screen resolution changes */
static int pce_gfx_mode(void)
{
    printf("PCE GFX: mode change → %dx%d\n", io.screen_w, io.screen_h);
    return 1;
}

/*
 * pce_gfx_draw — called by RefreshScreen() each frame.
 *
 * The engine has rendered into osd_gfx_buffer (8-bit indexed pixels, stride
 * XBUF_WIDTH=600).  We copy the visible portion centred into a 320×240
 * buffer then push to the display pipeline.
 *
 * Display runs asynchronously in a separate task so that PPA rotate+scale
 * and palette conversion overlap with the next frame's CPU emulation.
 */
#define PCE_DISP_BUFS 2
static uint8_t  *disp_fb[PCE_DISP_BUFS];       /* double-buffered 320×240 indexed */
static uint16_t *disp_pal_snap;                  /* palette snapshot (256 entries)  */
static int       disp_fb_idx   = 0;
static volatile bool disp_task_running = false;
static TaskHandle_t  disp_task_handle  = NULL;
static SemaphoreHandle_t disp_ready_sem = NULL;  /* display task finished previous frame */
static SemaphoreHandle_t disp_go_sem    = NULL;  /* new frame available for display    */

/* Timing instrumentation */
static int64_t pce_last_frame_end = 0;
static int64_t pce_emu_acc  = 0;
static int64_t pce_disp_acc = 0;
static int64_t pce_wait_acc = 0;

static void pce_display_task(void *arg)
{
    (void)arg;
    disp_task_running = true;
    while (disp_task_running) {
        if (xSemaphoreTake(disp_go_sem, pdMS_TO_TICKS(100)) == pdTRUE) {
            if (!disp_task_running) break;
            int idx = disp_fb_idx ^ 1;   /* previous buffer */
            ili9341_write_frame_prosystem(disp_fb[idx], disp_pal_snap);
            xSemaphoreGive(disp_ready_sem);
        }
    }
    vTaskDelete(NULL);
}

static volatile int pce_resume_diag = 0;  /* frames to print diag after resume */

static void pce_gfx_draw(void)
{
    if (!disp_fb[0]) return;

    int src_w = io.screen_w;
    int src_h = io.screen_h;
    if (src_w <= 0 || src_h <= 0) {
        if (pce_resume_diag) {
            printf("PCE DIAG: skip frame — src_w=%d src_h=%d\n", src_w, src_h);
            pce_resume_diag--;
        }
        return;
    }

    if (pce_resume_diag > 0) {
        /* Sample a few pixels from mid-screen in osd_gfx_buffer */
        int mid_y = src_h / 2;
        int mid_x = src_w / 2;
        uint8_t *mid_ptr = osd_gfx_buffer + mid_y * XBUF_WIDTH + mid_x;
        int nonzero = 0;
        for (int i = 0; i < src_w && i < 256; i++)
            if (osd_gfx_buffer[mid_y * XBUF_WIDTH + i]) nonzero++;
        printf("PCE DIAG frame: w=%d h=%d min_d=%d max_d=%d bg_w=%d bg_h=%d "
               "VDC5=0x%04x ctx_cr=0x%04x mid_px=[%d,%d,%d,%d,%d] nonzero=%d/256 "
               "Pal[1]=%d Pal[16]=%d pal565[1]=0x%04x\n",
               src_w, src_h,
               io.vdc_min_display, io.vdc_max_display,
               io.bg_w, io.bg_h,
               (unsigned)IO_VDC_05_CR.W, (unsigned)saved_gfx_context[0].cr,
               mid_ptr[-2], mid_ptr[-1], mid_ptr[0], mid_ptr[1], mid_ptr[2],
               nonzero,
               (int)Pal[1], (int)Pal[16], my_palette[1]);
        pce_resume_diag--;
    }

    int64_t t_wait_start = esp_timer_get_time();

    /* Wait for display task to finish the previous frame */
    xSemaphoreTake(disp_ready_sem, portMAX_DELAY);

    int64_t t_after_wait = esp_timer_get_time();
    pce_wait_acc += (t_after_wait - t_wait_start);

    uint8_t *centered_fb = disp_fb[disp_fb_idx];

    /* Clamp / centre horizontally */
    int x_off = 0;
    int dst_x = 0;
    int copy_w = src_w;
    if (src_w > PCE_FB_WIDTH) {
        x_off = (src_w - PCE_FB_WIDTH) / 2;
        copy_w = PCE_FB_WIDTH;
    } else if (src_w < PCE_FB_WIDTH) {
        dst_x = (PCE_FB_WIDTH - src_w) / 2;
    }

    /* Clamp vertically */
    int y_off = 0;
    int dst_y = 0;
    int copy_h = src_h;
    if (src_h > PCE_FB_HEIGHT) {
        y_off = (src_h - PCE_FB_HEIGHT) / 2;
        copy_h = PCE_FB_HEIGHT;
    } else if (src_h < PCE_FB_HEIGHT) {
        dst_y = (PCE_FB_HEIGHT - src_h) / 2;
    }

    /* Clear the border areas if not full-width/height */
    if (dst_x > 0 || dst_y > 0 || copy_w < PCE_FB_WIDTH || copy_h < PCE_FB_HEIGHT) {
        memset(centered_fb, 0, PCE_FB_WIDTH * PCE_FB_HEIGHT);
    }

    /* Copy visible scanlines */
    for (int y = 0; y < copy_h; y++) {
        uint8_t *src = osd_gfx_buffer + (y + y_off) * XBUF_WIDTH + x_off;
        uint8_t *dst = centered_fb + (y + dst_y) * PCE_FB_WIDTH + dst_x;
        memcpy(dst, src, copy_w);
    }

    /* Snapshot palette & signal display task */
    memcpy(disp_pal_snap, my_palette, 256 * sizeof(uint16_t));

    int64_t t_before_signal = esp_timer_get_time();
    if (pce_last_frame_end > 0) {
        pce_emu_acc += (t_before_signal - pce_last_frame_end);
    }

    xSemaphoreGive(disp_go_sem);    /* kick display task */
    disp_fb_idx ^= 1;

    pce_last_frame_end = esp_timer_get_time();

    /* Swap double buffer */
    current_framebuffer ^= 1;
    XBuf = framebuffer[current_framebuffer];
    osd_gfx_buffer = XBuf + 32 + 64 * XBUF_WIDTH;

    /* FPS counter + timing report */
    fps_frame_count++;
    if (fps_frame_count >= 60) {
        int64_t now = esp_timer_get_time();
        float elapsed_s = (now - fps_start_us) / 1000000.0f;
        printf("PCE FPS: %.1f  EMU=%.1fms  WAIT=%.1fms\n",
               fps_frame_count / elapsed_s,
               pce_emu_acc / (fps_frame_count * 1000.0f),
               pce_wait_acc / (fps_frame_count * 1000.0f));
        fps_start_us = now;
        fps_frame_count = 0;
        pce_emu_acc  = 0;
        pce_wait_acc = 0;
    }
}

static void pce_gfx_shut(void)
{
    printf("PCE GFX: shut\n");
}

/* Other required OSD gfx stubs */
void osd_gfx_set_message(char *message)
{
    printf("PCE MSG: %s\n", message ? message : "(null)");
}

uint16 osd_gfx_savepict(void)
{
    return 0;
}

/* update_display_task — called from osd_gfx_init_normal_mode in orig.
 * We don't use separate video tasks per width; our pce_gfx_draw handles
 * all widths.  This is a no-op. */
void update_display_task(int width)
{
    printf("PCE: update_display_task(%d) — handled inline\n", width);
}

/* ────────────────────────────────────────────────────────────────── */
/*  OSD Sound functions                                               */
/* ────────────────────────────────────────────────────────────────── */
int  osd_snd_init_sound(void) { return 1; }
void osd_snd_trash_sound(void) { }
void osd_snd_set_volume(uchar v) { (void)v; }


/* ────────────────────────────────────────────────────────────────── */
/*  OSD Machine functions                                             */
/* ────────────────────────────────────────────────────────────────── */
int  osd_init_machine(void) { return 0; }
void osd_shut_machine(void)  { }

/* ────────────────────────────────────────────────────────────────── */
/*  OSD Input functions                                               */
/* ────────────────────────────────────────────────────────────────── */
int  osd_init_input(void)        { return 0; }
void osd_shutdown_input(void)    { }
int  osd_init_netplay(void)      { return 0; }
void osd_shutdown_netplay(void)  { }
char osd_keypressed(void)        { return 0; }

/* ────────────────────────────────────────────────────────────────── */
/*  5×7 Bitmap font (for in-game menu & volume overlay)              */
/* ────────────────────────────────────────────────────────────────── */
static const uint8_t pce_font5x7[][7] = {
    /* ' ' */ {0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    /* 'A' */ {0x0E,0x11,0x11,0x1F,0x11,0x11,0x11},
    /* 'B' */ {0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E},
    /* 'C' */ {0x0E,0x11,0x10,0x10,0x10,0x11,0x0E},
    /* 'D' */ {0x1E,0x11,0x11,0x11,0x11,0x11,0x1E},
    /* 'E' */ {0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F},
    /* 'F' */ {0x1F,0x10,0x10,0x1E,0x10,0x10,0x10},
    /* 'G' */ {0x0E,0x11,0x10,0x17,0x11,0x11,0x0E},
    /* 'H' */ {0x11,0x11,0x11,0x1F,0x11,0x11,0x11},
    /* 'I' */ {0x0E,0x04,0x04,0x04,0x04,0x04,0x0E},
    /* 'J' */ {0x07,0x02,0x02,0x02,0x02,0x12,0x0C},
    /* 'K' */ {0x11,0x12,0x14,0x18,0x14,0x12,0x11},
    /* 'L' */ {0x10,0x10,0x10,0x10,0x10,0x10,0x1F},
    /* 'M' */ {0x11,0x1B,0x15,0x15,0x11,0x11,0x11},
    /* 'N' */ {0x11,0x19,0x15,0x13,0x11,0x11,0x11},
    /* 'O' */ {0x0E,0x11,0x11,0x11,0x11,0x11,0x0E},
    /* 'P' */ {0x1E,0x11,0x11,0x1E,0x10,0x10,0x10},
    /* 'Q' */ {0x0E,0x11,0x11,0x11,0x15,0x12,0x0D},
    /* 'R' */ {0x1E,0x11,0x11,0x1E,0x14,0x12,0x11},
    /* 'S' */ {0x0E,0x11,0x10,0x0E,0x01,0x11,0x0E},
    /* 'T' */ {0x1F,0x04,0x04,0x04,0x04,0x04,0x04},
    /* 'U' */ {0x11,0x11,0x11,0x11,0x11,0x11,0x0E},
    /* 'V' */ {0x11,0x11,0x11,0x11,0x11,0x0A,0x04},
    /* 'W' */ {0x11,0x11,0x11,0x15,0x15,0x1B,0x11},
    /* 'X' */ {0x11,0x11,0x0A,0x04,0x0A,0x11,0x11},
    /* 'Y' */ {0x11,0x11,0x0A,0x04,0x04,0x04,0x04},
    /* 'Z' */ {0x1F,0x01,0x02,0x04,0x08,0x10,0x1F},
    /* ':' */ {0x00,0x04,0x00,0x00,0x00,0x04,0x00},
    /* '>' */ {0x10,0x08,0x04,0x02,0x04,0x08,0x10},
    /* '0' */ {0x0E,0x11,0x13,0x15,0x19,0x11,0x0E},
    /* '1' */ {0x04,0x0C,0x04,0x04,0x04,0x04,0x0E},
    /* '2' */ {0x0E,0x11,0x01,0x06,0x08,0x10,0x1F},
    /* '3' */ {0x0E,0x11,0x01,0x06,0x01,0x11,0x0E},
    /* '4' */ {0x02,0x06,0x0A,0x12,0x1F,0x02,0x02},
    /* '5' */ {0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E},
    /* '6' */ {0x06,0x08,0x10,0x1E,0x11,0x11,0x0E},
    /* '7' */ {0x1F,0x01,0x02,0x04,0x04,0x04,0x04},
    /* '8' */ {0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E},
    /* '9' */ {0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C},
    /* '-' */ {0x00,0x00,0x00,0x1F,0x00,0x00,0x00},
    /* '/' */ {0x01,0x02,0x02,0x04,0x08,0x08,0x10},
};

static int pce_font_index(char c) {
    if (c == ' ') return 0;
    if (c >= 'A' && c <= 'Z') return 1 + (c - 'A');
    if (c >= 'a' && c <= 'z') return 1 + (c - 'a');
    if (c == ':') return 27;
    if (c == '>') return 28;
    if (c >= '0' && c <= '9') return 29 + (c - '0');
    if (c == '-') return 39;
    if (c == '/') return 40;
    return 0; /* space for unknown */
}

/* Draw a single character (foreground only — transparent bg) */
static void pce_menu_draw_char(uint16_t *fb, int px, int py, char c, uint16_t color)
{
    int idx = pce_font_index(c);
    for (int row = 0; row < 7; row++) {
        uint8_t bits = pce_font5x7[idx][row];
        int yy = py + row;
        if (yy < 0 || yy >= PCE_FB_HEIGHT) continue;
        for (int col = 0; col < 5; col++) {
            if (bits & (0x10 >> col)) {
                int xx = px + col;
                if (xx >= 0 && xx < PCE_FB_WIDTH)
                    fb[yy * PCE_FB_WIDTH + xx] = color;
            }
        }
    }
}

/* Draw a string at (px,py). Each char is 6px wide (5 + 1 gap). */
static void pce_menu_draw_string(uint16_t *fb, int px, int py, const char *str, uint16_t color)
{
    while (*str) {
        pce_menu_draw_char(fb, px, py, *str, color);
        px += 6;
        str++;
    }
}

/* Fill a rectangle on the 320×240 framebuffer */
static void pce_menu_fill_rect(uint16_t *fb, int x, int y, int w, int h, uint16_t color)
{
    for (int row = y; row < y + h && row < PCE_FB_HEIGHT; row++) {
        if (row < 0) continue;
        for (int col = x; col < x + w && col < PCE_FB_WIDTH; col++) {
            if (col < 0) continue;
            fb[row * PCE_FB_WIDTH + col] = color;
        }
    }
}

/* RGB565 colors */
#define PCE_COLOR_BLACK   0x0000
#define PCE_COLOR_WHITE   0xFFFF
#define PCE_COLOR_YELLOW  0xE0FF
#define PCE_COLOR_DKGRAY  0x2108
#define PCE_COLOR_GREEN   0xE007
#define PCE_COLOR_CYAN    0xFF07

/* ────────────────────────────────────────────────────────────────── */
/*  Volume overlay                                                    */
/* ────────────────────────────────────────────────────────────────── */
static void pce_show_volume_overlay(void)
{
    uint16_t *fb = display_get_framebuffer();
    if (!fb) return;

    static const char *level_names[ODROID_VOLUME_LEVEL_COUNT] = {
        "MUTE", "25", "50", "75", "100"
    };

    int level = (int)odroid_audio_volume_get();
    int timeout = 25;  /* ~2s at 80ms per frame */

    odroid_gamepad_state prev;
    odroid_input_gamepad_read(&prev);

    /* Debounce: wait for Y release */
    for (int i = 0; i < 100; i++) {
        odroid_gamepad_state tmp;
        odroid_input_gamepad_read(&tmp);
        if (!tmp.values[ODROID_INPUT_VOLUME]) break;
        usleep(10000);
    }
    odroid_input_gamepad_read(&prev);

    while (timeout > 0) {
        odroid_display_lock_nes_display();

        int box_w = 140, box_h = 34;
        int box_x = (PCE_FB_WIDTH - box_w) / 2;
        int box_y = 8;

        /* Background + border */
        pce_menu_fill_rect(fb, box_x, box_y, box_w, box_h, PCE_COLOR_BLACK);
        pce_menu_fill_rect(fb, box_x, box_y, box_w, 1, PCE_COLOR_WHITE);
        pce_menu_fill_rect(fb, box_x, box_y + box_h - 1, box_w, 1, PCE_COLOR_WHITE);
        pce_menu_fill_rect(fb, box_x, box_y, 1, box_h, PCE_COLOR_WHITE);
        pce_menu_fill_rect(fb, box_x + box_w - 1, box_y, 1, box_h, PCE_COLOR_WHITE);

        /* Title */
        char title[32];
        snprintf(title, sizeof(title), "VOLUME: %s", level_names[level]);
        int title_w = strlen(title) * 6;
        pce_menu_draw_string(fb, box_x + (box_w - title_w) / 2, box_y + 4, title, PCE_COLOR_YELLOW);

        /* Bar */
        int bar_x = box_x + 10, bar_y = box_y + 16;
        int bar_w = box_w - 20, bar_h = 10;
        pce_menu_fill_rect(fb, bar_x, bar_y, bar_w, bar_h, PCE_COLOR_DKGRAY);
        if (level > 0) {
            int fill_w = (bar_w * level) / (ODROID_VOLUME_LEVEL_COUNT - 1);
            uint16_t bar_color = (level <= 1) ? PCE_COLOR_GREEN :
                                 (level <= 3) ? PCE_COLOR_CYAN : PCE_COLOR_YELLOW;
            pce_menu_fill_rect(fb, bar_x, bar_y, fill_w, bar_h, bar_color);
        }
        /* Bar border */
        pce_menu_fill_rect(fb, bar_x, bar_y, bar_w, 1, PCE_COLOR_WHITE);
        pce_menu_fill_rect(fb, bar_x, bar_y + bar_h - 1, bar_w, 1, PCE_COLOR_WHITE);
        pce_menu_fill_rect(fb, bar_x, bar_y, 1, bar_h, PCE_COLOR_WHITE);
        pce_menu_fill_rect(fb, bar_x + bar_w - 1, bar_y, 1, bar_h, PCE_COLOR_WHITE);
        for (int i = 1; i < ODROID_VOLUME_LEVEL_COUNT - 1; i++) {
            int sx = bar_x + (bar_w * i) / (ODROID_VOLUME_LEVEL_COUNT - 1);
            pce_menu_fill_rect(fb, sx, bar_y, 1, bar_h, PCE_COLOR_WHITE);
        }

        display_flush_force();
        odroid_display_unlock_nes_display();

        usleep(80000);
        odroid_gamepad_state state;
        odroid_input_gamepad_read(&state);

        bool changed = false;
        if (state.values[ODROID_INPUT_LEFT] && !prev.values[ODROID_INPUT_LEFT]) {
            if (level > 0) { level--; changed = true; }
            timeout = 25;
        }
        if (state.values[ODROID_INPUT_RIGHT] && !prev.values[ODROID_INPUT_RIGHT]) {
            if (level < ODROID_VOLUME_LEVEL_COUNT - 1) { level++; changed = true; }
            timeout = 25;
        }
        if (state.values[ODROID_INPUT_VOLUME] && !prev.values[ODROID_INPUT_VOLUME]) {
            level = (level + 1) % ODROID_VOLUME_LEVEL_COUNT;
            changed = true;
            timeout = 25;
        }
        if ((state.values[ODROID_INPUT_A] && !prev.values[ODROID_INPUT_A]) ||
            (state.values[ODROID_INPUT_B] && !prev.values[ODROID_INPUT_B])) {
            timeout = 0;
        }
        if (changed) {
            odroid_audio_volume_set(level);
            odroid_settings_Volume_set(level);
        }
        prev = state;
        timeout--;
    }
}

/* ────────────────────────────────────────────────────────────────── */
/*  In-game menu                                                      */
/* ────────────────────────────────────────────────────────────────── */
/* Forward declarations — implementations below after QuickState stubs */
static bool pce_check_save_exists(void);
static bool pce_delete_save(void);
static bool PCE_SaveState(void);
static bool PCE_LoadState(void);

/* Callback for post-gfx_init state resume — called once from gfx_init()
   inside exe_go(), BEFORE the first scanline runs. */
static void pce_resume_load_cb(void)
{
    if (PCE_LoadState()) {
        printf("PCE: Resumed from save state (post-init)\n");
        pce_resume_diag = 5;  /* print diag for first 5 frames */
    }
}

#define PCE_MENU_RESUME     0
#define PCE_MENU_RESTART    1
#define PCE_MENU_SAVE       2
#define PCE_MENU_RELOAD     3
#define PCE_MENU_OVERWRITE  4
#define PCE_MENU_DELETE     5
#define PCE_MENU_EXIT       6

/* Returns: true = keep running, false = exit game */
static bool show_game_menu(void)
{
    uint16_t *fb = display_get_framebuffer();
    if (!fb) return true;

    bool has_save = pce_check_save_exists();

    const char *labels[8];
    int ids[8];
    int count = 0;

    labels[count] = "Resume Game";      ids[count] = PCE_MENU_RESUME;    count++;
    labels[count] = "Restart Game";     ids[count] = PCE_MENU_RESTART;   count++;
    labels[count] = "Save Game";        ids[count] = PCE_MENU_SAVE;      count++;
    if (has_save) {
        labels[count] = "Reload Game";      ids[count] = PCE_MENU_RELOAD;    count++;
        labels[count] = "Overwrite Save";   ids[count] = PCE_MENU_OVERWRITE; count++;
        labels[count] = "Delete Save";      ids[count] = PCE_MENU_DELETE;    count++;
    }
    labels[count] = "Exit Game";        ids[count] = PCE_MENU_EXIT;      count++;

    int selected = 0;

    odroid_gamepad_state prev;
    odroid_input_gamepad_read(&prev);

    /* Debounce: wait for MENU button release */
    for (int i = 0; i < 200; i++) {
        odroid_gamepad_state tmp;
        odroid_input_gamepad_read(&tmp);
        if (!tmp.values[ODROID_INPUT_MENU]) break;
        usleep(10000);
    }
    odroid_input_gamepad_read(&prev);

    bool keep_running = true;
    bool menu_active = true;
    const char *flash_msg = NULL;
    int flash_timer = 0;

    while (menu_active) {
        odroid_display_lock_nes_display();

        int box_w = 160;
        int box_h = 20 + count * 14 + 10;
        int box_x = (PCE_FB_WIDTH - box_w) / 2;
        int box_y = (PCE_FB_HEIGHT - box_h) / 2;

        /* Dark background + white border */
        pce_menu_fill_rect(fb, box_x, box_y, box_w, box_h, PCE_COLOR_BLACK);
        pce_menu_fill_rect(fb, box_x, box_y, box_w, 1, PCE_COLOR_WHITE);
        pce_menu_fill_rect(fb, box_x, box_y + box_h - 1, box_w, 1, PCE_COLOR_WHITE);
        pce_menu_fill_rect(fb, box_x, box_y, 1, box_h, PCE_COLOR_WHITE);
        pce_menu_fill_rect(fb, box_x + box_w - 1, box_y, 1, box_h, PCE_COLOR_WHITE);

        /* Title */
        pce_menu_draw_string(fb, box_x + (box_w - 9 * 6) / 2, box_y + 5, "GAME MENU", PCE_COLOR_YELLOW);

        /* Options */
        for (int i = 0; i < count; i++) {
            int oy = box_y + 18 + i * 14;
            int ox = box_x + 16;
            uint16_t color = (i == selected) ? PCE_COLOR_YELLOW : PCE_COLOR_WHITE;

            pce_menu_fill_rect(fb, box_x + 2, oy - 1, box_w - 4, 10, PCE_COLOR_BLACK);
            if (i == selected)
                pce_menu_draw_char(fb, box_x + 6, oy, '>', PCE_COLOR_YELLOW);
            pce_menu_draw_string(fb, ox, oy, labels[i], color);
        }

        /* Flash message */
        if (flash_msg && flash_timer > 0) {
            int fw = strlen(flash_msg) * 6;
            int fx = box_x + (box_w - fw) / 2;
            int fy = box_y + box_h - 12;
            pce_menu_fill_rect(fb, box_x + 2, fy - 2, box_w - 4, 12, PCE_COLOR_BLACK);
            pce_menu_draw_string(fb, fx, fy, flash_msg, PCE_COLOR_GREEN);
            flash_timer--;
            if (flash_timer == 0) flash_msg = NULL;
        }

        display_flush_force();
        odroid_display_unlock_nes_display();

        usleep(60000);
        odroid_gamepad_state js;
        odroid_input_gamepad_read(&js);

        /* D-pad navigation */
        if (js.values[ODROID_INPUT_UP] && !prev.values[ODROID_INPUT_UP])
            selected = (selected - 1 + count) % count;
        if (js.values[ODROID_INPUT_DOWN] && !prev.values[ODROID_INPUT_DOWN])
            selected = (selected + 1) % count;

        /* B or MENU = resume */
        if ((js.values[ODROID_INPUT_B] && !prev.values[ODROID_INPUT_B]) ||
            (js.values[ODROID_INPUT_MENU] && !prev.values[ODROID_INPUT_MENU])) {
            menu_active = false;
            keep_running = true;
        }

        /* A = select option */
        if (js.values[ODROID_INPUT_A] && !prev.values[ODROID_INPUT_A]) {
            switch (ids[selected]) {
                case PCE_MENU_RESUME:
                    menu_active = false;
                    keep_running = true;
                    break;

                case PCE_MENU_RESTART:
                    printf("PCE Menu: Restart Game\n");
                    esp_restart();
                    break;

                case PCE_MENU_SAVE:
                    printf("PCE Menu: Save Game\n");
                    if (PCE_SaveState()) {
                        flash_msg = "Saved!";
                        if (!has_save) {
                            has_save = true;
                            count = 0;
                            labels[count] = "Resume Game";      ids[count] = PCE_MENU_RESUME;    count++;
                            labels[count] = "Restart Game";     ids[count] = PCE_MENU_RESTART;   count++;
                            labels[count] = "Save Game";        ids[count] = PCE_MENU_SAVE;      count++;
                            labels[count] = "Reload Game";      ids[count] = PCE_MENU_RELOAD;    count++;
                            labels[count] = "Overwrite Save";   ids[count] = PCE_MENU_OVERWRITE; count++;
                            labels[count] = "Delete Save";      ids[count] = PCE_MENU_DELETE;    count++;
                            labels[count] = "Exit Game";        ids[count] = PCE_MENU_EXIT;      count++;
                        }
                    } else {
                        flash_msg = "Error!";
                    }
                    flash_timer = 15;
                    break;

                case PCE_MENU_RELOAD:
                    printf("PCE Menu: Reload Game\n");
                    if (PCE_LoadState())
                        flash_msg = "Loaded!";
                    else
                        flash_msg = "Error!";
                    flash_timer = 15;
                    menu_active = false;
                    keep_running = true;
                    break;

                case PCE_MENU_OVERWRITE:
                    printf("PCE Menu: Overwrite Save\n");
                    flash_msg = PCE_SaveState() ? "Saved!" : "Error!";
                    flash_timer = 15;
                    break;

                case PCE_MENU_DELETE:
                    printf("PCE Menu: Delete Save\n");
                    if (pce_delete_save()) {
                        flash_msg = "Deleted!";
                        has_save = false;
                        count = 0;
                        labels[count] = "Resume Game";    ids[count] = PCE_MENU_RESUME;  count++;
                        labels[count] = "Restart Game";   ids[count] = PCE_MENU_RESTART; count++;
                        labels[count] = "Save Game";      ids[count] = PCE_MENU_SAVE;    count++;
                        labels[count] = "Exit Game";      ids[count] = PCE_MENU_EXIT;    count++;
                        if (selected >= count) selected = count - 1;
                    } else {
                        flash_msg = "Error!";
                    }
                    flash_timer = 15;
                    break;

                case PCE_MENU_EXIT:
                    menu_active = false;
                    keep_running = false;
                    break;
            }
        }

        prev = js;
    }

    /* Wait for all buttons released */
    odroid_gamepad_state js;
    do {
        usleep(20000);
        odroid_input_gamepad_read(&js);
    } while (js.values[ODROID_INPUT_A] || js.values[ODROID_INPUT_B] ||
            js.values[ODROID_INPUT_MENU] || js.values[ODROID_INPUT_UP] ||
            js.values[ODROID_INPUT_DOWN]);
    usleep(50000);

    return keep_running;
}

/* ────────────────────────────────────────────────────────────────── */
/*  OSD Keyboard (input polling — called by engine every frame)       */
/* ────────────────────────────────────────────────────────────────── */
#define JOY_A       0x01
#define JOY_B       0x02
#define JOY_SELECT  0x04
#define JOY_RUN     0x08
#define JOY_UP      0x10
#define JOY_RIGHT   0x20
#define JOY_DOWN    0x40
#define JOY_LEFT    0x80

int osd_keyboard(void)
{
    odroid_gamepad_state joystick;
    odroid_input_gamepad_read(&joystick);

    /* MENU button handling */
    if (ignoreMenuButton) {
        ignoreMenuButton = previousJoystickState.values[ODROID_INPUT_MENU];
    }
    if (!ignoreMenuButton &&
        previousJoystickState.values[ODROID_INPUT_MENU] &&
        joystick.values[ODROID_INPUT_MENU]) {
        menuButtonFrameCount++;
    } else {
        menuButtonFrameCount = 0;
    }

    /* Long press (>2s) — quit */
    if (menuButtonFrameCount > 60 * 2) {
        pce_quit_flag = true;
        return 1;  /* non-zero triggers INT_QUIT in engine */
    }

    /* Short press — show menu */
    if (!ignoreMenuButton &&
        previousJoystickState.values[ODROID_INPUT_MENU] &&
        !joystick.values[ODROID_INPUT_MENU]) {
        if (!show_game_menu()) {
            pce_quit_flag = true;
            return 1;  /* non-zero triggers INT_QUIT in engine */
        }
        /* resume — re-read gamepad to avoid stale A press leaking */
        odroid_input_gamepad_read(&joystick);
        io.JOY[0] = 0;  /* suppress any residual input this frame */
    }

    /* Volume button (Y) — edge-triggered */
    if (!previousJoystickState.values[ODROID_INPUT_VOLUME] &&
        joystick.values[ODROID_INPUT_VOLUME]) {
        pce_show_volume_overlay();
        odroid_input_gamepad_read(&joystick);
    }

    previousJoystickState = joystick;

    /* Map gamepad → PC Engine joypad bits */
    uint8_t rc = 0;
    if (joystick.values[ODROID_INPUT_LEFT])   rc |= JOY_LEFT;
    if (joystick.values[ODROID_INPUT_RIGHT])  rc |= JOY_RIGHT;
    if (joystick.values[ODROID_INPUT_UP])     rc |= JOY_UP;
    if (joystick.values[ODROID_INPUT_DOWN])   rc |= JOY_DOWN;
    if (joystick.values[ODROID_INPUT_A])      rc |= JOY_A;     /* I button */
    if (joystick.values[ODROID_INPUT_B])      rc |= JOY_B;     /* II button */
    if (joystick.values[ODROID_INPUT_START])  rc |= JOY_RUN;
    if (joystick.values[ODROID_INPUT_SELECT]) rc |= JOY_SELECT;

    io.JOY[0] = rc;

    return 0;
}

/* ────────────────────────────────────────────────────────────────── */
/*  Audio task — mixes 6 PSG channels + submits to codec               */
/* ────────────────────────────────────────────────────────────────── */
extern void WriteBuffer(char *buf, int ch, unsigned dwSize);

static void audioTask(void *arg)
{
    audioTaskIsRunning = true;
    printf("PCE Audio: STARTED\n");
    uint8_t buf_idx = 0;

    while (1) {
        /* Check for stop signal */
        void *sig;
        if (xQueuePeek(audioQueue, &sig, 0) == pdTRUE) {
            if (sig == TASK_BREAK) break;
            xQueueReceive(audioQueue, &sig, portMAX_DELAY);
        }

        if (pce_quit_flag) break;

        /* Fill each channel's buffer */
        for (int i = 0; i < AUDIO_CHANNELS; i++) {
            WriteBuffer(sbuf[i], i, AUDIO_BUFFER_SIZE / 2);
        }

        /* Mix all channels with master volume.
           WriteBuffer() produces interleaved stereo bytes: L, R, L, R, ...
           Each byte is a SIGNED value (-127..127) but 'char' is unsigned on
           RISC-V, so we must cast via (signed char) to get correct sign
           extension.  Step by 2 to process each L/R pair correctly.

           Use int accumulators to avoid any overflow, and clamp the scaled
           result to int16 range before writing.  No usleep — the blocking
           I2S write in odroid_audio_submit() provides natural pacing. */
        int lvol = (int)((io.psg_volume >> 4) * 1.22f);
        int rvol = (int)((io.psg_volume & 0x0F) * 1.22f);

        short *p = sbuf_mix[buf_idx];
        int num_frames = AUDIO_BUFFER_SIZE / 4;  /* 1024 stereo pairs */
        for (int i = 0; i < num_frames; i++) {
            int idx = i * 2;
            int lval = 0, rval = 0;
            for (int j = 0; j < AUDIO_CHANNELS; j++) {
                lval += (int)(signed char)sbuf[j][idx];
                rval += (int)(signed char)sbuf[j][idx + 1];
            }
            int lo = lval * lvol;
            int ro = rval * rvol;
            /* Clamp to int16 range */
            if (lo > 32767)  lo = 32767;  else if (lo < -32768) lo = -32768;
            if (ro > 32767)  ro = 32767;  else if (ro < -32768) ro = -32768;
            *p++ = (short)lo;
            *p++ = (short)ro;
        }

        odroid_audio_submit(sbuf_mix[buf_idx], num_frames);
        buf_idx ^= 1;
    }

    void *sig;
    xQueueReceive(audioQueue, &sig, portMAX_DELAY);
    audioTaskIsRunning = false;
    printf("PCE Audio: STOPPED\n");
    vTaskDelete(NULL);
}

static void start_audio(void)
{
    if (audioTaskIsRunning) return;
    xTaskCreatePinnedToCore(audioTask, "pceAudio", 4096, NULL, 5, &audioTaskHandle, 1);
    while (!audioTaskIsRunning) vTaskDelay(1);
}

static void stop_audio(void)
{
    if (!audioTaskIsRunning) return;
    void *sig = TASK_BREAK;
    xQueueSend(audioQueue, &sig, portMAX_DELAY);
    while (audioTaskIsRunning) vTaskDelay(1);
}

/* ────────────────────────────────────────────────────────────────── */
/*  Save / Load state                                                 */
/* ────────────────────────────────────────────────────────────────── */
bool QuickLoadState(FILE *f) { return false; }
bool QuickSaveState(FILE *f) { return false; }
void QuickSaveSetBuffer(void *data) { (void)data; }

#define PCE_STATE_MAGIC  0x50434553   /* "PCES" */
#define PCE_STATE_VER    4

extern const char* SD_BASE_PATH;

static char *pce_get_save_path(void)
{
    char *romName = odroid_settings_RomFilePath_get();
    if (!romName) return NULL;
    char *fileName = odroid_util_GetFileName(romName);
    free(romName);
    if (!fileName) return NULL;
    char pathBuf[256];
    snprintf(pathBuf, sizeof(pathBuf), "/sd/odroid/data/pce/%s.sav", fileName);
    free(fileName);
    return strdup(pathBuf);
}

static bool pce_check_save_exists(void)
{
    char *path = pce_get_save_path();
    if (!path) return false;
    struct stat st;
    bool exists = (stat(path, &st) == 0);
    free(path);
    return exists;
}

static bool pce_delete_save(void)
{
    char *path = pce_get_save_path();
    if (!path) return false;
    bool ok = (unlink(path) == 0);
    free(path);
    return ok;
}

/* Write helper — returns false on error */
static bool wr(FILE *f, const void *data, size_t len)
{
    return fwrite(data, 1, len, f) == len;
}

/* Read helper — returns false on error */
static bool rd(FILE *f, void *data, size_t len)
{
    return fread(data, 1, len, f) == len;
}

static bool PCE_SaveState(void)
{
    char *path = pce_get_save_path();
    if (!path) return false;

    mkdir("/sd/odroid", 0775);
    mkdir("/sd/odroid/data", 0775);
    mkdir("/sd/odroid/data/pce", 0775);

    FILE *f = fopen(path, "wb");
    free(path);
    if (!f) return false;

    uint32_t magic = PCE_STATE_MAGIC;
    uint32_t ver   = PCE_STATE_VER;
    bool ok = true;

    ok = ok && wr(f, &magic, 4);
    ok = ok && wr(f, &ver, 4);

    /* CPU registers — use the actual engine globals, not the struct
       fields (which are NOT aliased on this platform).  reg_pc is
       uint32 in the global but we store 16-bit in the file. */
    { uint16_t pc16 = (uint16_t)reg_pc;
      ok = ok && wr(f, &pc16, 2); }
    { uchar ra = reg_a; ok = ok && wr(f, &ra, 1); }
    { uchar rx = reg_x; ok = ok && wr(f, &rx, 1); }
    { uchar ry = reg_y; ok = ok && wr(f, &ry, 1); }
    { uchar rp = reg_p; ok = ok && wr(f, &rp, 1); }
    { uchar rs = reg_s; ok = ok && wr(f, &rs, 1); }

    /* Cycle counters — cyclecount/cyclecountold/scanline ARE aliased
       via pointers, but cycles_ is a standalone global */
    ok = ok && wr(f, &hard_pce->s_cyclecount,    4);
    ok = ok && wr(f, &hard_pce->s_cyclecountold,  4);
    ok = ok && wr(f, &cycles_,                    4);
    ok = ok && wr(f, &hard_pce->s_scanline,       4);

    /* MMR */
    ok = ok && wr(f, hard_pce->mmr, 8);

    /* RAM, WRAM, VRAM */
    ok = ok && wr(f, hard_pce->RAM,  0x8000);
    ok = ok && wr(f, hard_pce->WRAM, 0x2000);
    ok = ok && wr(f, hard_pce->VRAM, VRAMSIZE);

    /* SPRAM, Pal */
    ok = ok && wr(f, hard_pce->SPRAM, 64 * 4 * sizeof(uint16));
    ok = ok && wr(f, hard_pce->Pal,   512);

    /* vchange / vchanges */
    ok = ok && wr(f, hard_pce->vchange,  VRAMSIZE / 32);
    ok = ok && wr(f, hard_pce->vchanges, VRAMSIZE / 128);

    /* IO: VCE palette */
    ok = ok && wr(f, hard_pce->s_io.VCE, 0x200 * sizeof(pair));

    /* IO: VCE register + ratch */
    ok = ok && wr(f, &hard_pce->s_io.vce_reg,   sizeof(pair));
    ok = ok && wr(f, &hard_pce->s_io.vce_ratch, 1);

    /* IO: VDC registers (array in io struct — kept for compat) */
    ok = ok && wr(f, hard_pce->s_io.VDC, 32 * sizeof(pair));

    /* IO: VDC standalone vars (MY_VDC_VARS — the ACTUAL live registers)
       Patch IO_VDC_05_CR before writing: during VBLANK the game may have
       zeroed CR, so use pce_display_cr captured at display start. */
    pair saved_vdc05_standalone = IO_VDC_05_CR;
    if (pce_display_cr)
        IO_VDC_05_CR.W = pce_display_cr;

    ok = ok && wr(f, &IO_VDC_00_MAWR,  sizeof(pair));
    ok = ok && wr(f, &IO_VDC_01_MARR,  sizeof(pair));
    ok = ok && wr(f, &IO_VDC_02_VWR,   sizeof(pair));
    ok = ok && wr(f, &IO_VDC_03_vdc3,  sizeof(pair));
    ok = ok && wr(f, &IO_VDC_04_vdc4,  sizeof(pair));
    ok = ok && wr(f, &IO_VDC_05_CR,    sizeof(pair));
    ok = ok && wr(f, &IO_VDC_06_RCR,   sizeof(pair));
    ok = ok && wr(f, &IO_VDC_07_BXR,   sizeof(pair));
    ok = ok && wr(f, &IO_VDC_08_BYR,   sizeof(pair));
    ok = ok && wr(f, &IO_VDC_09_MWR,   sizeof(pair));
    ok = ok && wr(f, &IO_VDC_0A_HSR,   sizeof(pair));
    ok = ok && wr(f, &IO_VDC_0B_HDR,   sizeof(pair));
    ok = ok && wr(f, &IO_VDC_0C_VPR,   sizeof(pair));
    ok = ok && wr(f, &IO_VDC_0D_VDW,   sizeof(pair));
    ok = ok && wr(f, &IO_VDC_0E_VCR,   sizeof(pair));
    ok = ok && wr(f, &IO_VDC_0F_DCR,   sizeof(pair));
    ok = ok && wr(f, &IO_VDC_10_SOUR,  sizeof(pair));
    ok = ok && wr(f, &IO_VDC_11_DISTR, sizeof(pair));
    ok = ok && wr(f, &IO_VDC_12_LENR,  sizeof(pair));
    ok = ok && wr(f, &IO_VDC_13_SATB,  sizeof(pair));
    ok = ok && wr(f, &IO_VDC_14,       sizeof(pair));

    /* Restore live IO_VDC_05_CR so emulation continues unaffected */
    IO_VDC_05_CR = saved_vdc05_standalone;

    ok = ok && wr(f, &hard_pce->s_io.vdc_inc,          2);
    ok = ok && wr(f, &hard_pce->s_io.vdc_raster_count, 2);
    ok = ok && wr(f, &hard_pce->s_io.vdc_reg,          1);
    ok = ok && wr(f, &hard_pce->s_io.vdc_status,       1);
    ok = ok && wr(f, &hard_pce->s_io.vdc_ratch,        1);
    ok = ok && wr(f, &hard_pce->s_io.vdc_satb,         1);
    ok = ok && wr(f, &hard_pce->s_io.vdc_pendvsync,    1);
    ok = ok && wr(f, &hard_pce->s_io.bg_h,             4);
    ok = ok && wr(f, &hard_pce->s_io.bg_w,             4);
    ok = ok && wr(f, &hard_pce->s_io.screen_w,         4);
    ok = ok && wr(f, &hard_pce->s_io.screen_h,         4);
    ok = ok && wr(f, &hard_pce->s_io.scroll_y,         4);
    ok = ok && wr(f, &hard_pce->s_io.minline,          4);
    ok = ok && wr(f, &hard_pce->s_io.maxline,          4);
    ok = ok && wr(f, &hard_pce->s_io.vdc_min_display,  2);
    ok = ok && wr(f, &hard_pce->s_io.vdc_max_display,  2);

    /* IO: Joypad */
    ok = ok && wr(f, hard_pce->s_io.JOY, 16);
    ok = ok && wr(f, &hard_pce->s_io.joy_select,  1);
    ok = ok && wr(f, &hard_pce->s_io.joy_counter, 1);

    /* IO: PSG */
    ok = ok && wr(f, hard_pce->s_io.PSG,  sizeof(hard_pce->s_io.PSG));
    ok = ok && wr(f, hard_pce->s_io.wave, sizeof(hard_pce->s_io.wave));
    ok = ok && wr(f, &hard_pce->s_io.psg_ch,        1);
    ok = ok && wr(f, &hard_pce->s_io.psg_volume,    1);
    ok = ok && wr(f, &hard_pce->s_io.psg_lfo_freq,  1);
    ok = ok && wr(f, &hard_pce->s_io.psg_lfo_ctrl,  1);
    for (int i = 0; i < 6; i++)
        ok = ok && wr(f, hard_pce->s_io.psg_da_data[i], PSG_DIRECT_ACCESS_BUFSIZE);
    ok = ok && wr(f, hard_pce->s_io.psg_da_index, sizeof(hard_pce->s_io.psg_da_index));
    ok = ok && wr(f, hard_pce->s_io.psg_da_count, sizeof(hard_pce->s_io.psg_da_count));

    /* IO: Timer, IRQ */
    ok = ok && wr(f, &hard_pce->s_io.timer_reload,  1);
    ok = ok && wr(f, &hard_pce->s_io.timer_start,   1);
    ok = ok && wr(f, &hard_pce->s_io.timer_counter, 1);
    ok = ok && wr(f, &hard_pce->s_io.irq_mask,      1);
    ok = ok && wr(f, &hard_pce->s_io.irq_status,    1);

    /* IO: io_buffer */
    ok = ok && wr(f, &hard_pce->s_io.io_buffer, 1);

    /* GFX context — the rendering engine caches VDC control register
       (background/sprite enable bits) in saved_gfx_context[].cr.
       The save may happen during VBLANK when the game has temporarily
       disabled BG/sprites (e.g. for DMA).  Use pce_display_cr which
       was captured at the start of the visible area instead. */
    {
        uint16 orig_ctx_cr   = saved_gfx_context[0].cr;
        pair   orig_vdc5_io  = hard_pce->s_io.VDC[5];

        if (pce_display_cr)
            saved_gfx_context[0].cr = pce_display_cr;
        hard_pce->s_io.VDC[5].W = saved_gfx_context[0].cr;

        ok = ok && wr(f, saved_gfx_context,
                      sizeof(gfx_context) * MAX_GFX_CONTEXT_SLOT_NUMBER);
        ok = ok && wr(f, &gfx_need_redraw, sizeof(gfx_need_redraw));

        /* Restore live state so emulation is unaffected */
        saved_gfx_context[0].cr = orig_ctx_cr;
        hard_pce->s_io.VDC[5]  = orig_vdc5_io;
    }

    fclose(f);
    if (!ok) printf("PCE SaveState: write error\n");
    else     printf("PCE SaveState: OK — display_cr=0x%04x\n",
                    (unsigned)pce_display_cr);
    return ok;
}

static bool PCE_LoadState(void)
{
    char *path = pce_get_save_path();
    if (!path) return false;

    FILE *f = fopen(path, "rb");
    free(path);
    if (!f) return false;

    uint32_t magic, ver;
    bool ok = true;

    ok = ok && rd(f, &magic, 4);
    ok = ok && rd(f, &ver, 4);
    printf("PCE LoadState: magic=0x%08lx (expect 0x%08x) ver=%lu (expect %u)\n",
           (unsigned long)magic, PCE_STATE_MAGIC, (unsigned long)ver, PCE_STATE_VER);
    if (!ok || magic != PCE_STATE_MAGIC || ver != PCE_STATE_VER) {
        printf("PCE LoadState: bad magic/version — REJECTED\n");
        fclose(f);
        return false;
    }

    /* CPU registers — read into the actual engine globals */
    { uint16_t pc16 = 0;
      ok = ok && rd(f, &pc16, 2);
      reg_pc = (uint32)pc16; }
    { uchar ra = 0; ok = ok && rd(f, &ra, 1); reg_a = ra; }
    { uchar rx = 0; ok = ok && rd(f, &rx, 1); reg_x = rx; }
    { uchar ry = 0; ok = ok && rd(f, &ry, 1); reg_y = ry; }
    { uchar rp = 0; ok = ok && rd(f, &rp, 1); reg_p = rp; }
    { uchar rs = 0; ok = ok && rd(f, &rs, 1); reg_s = rs; }

    /* Cycle counters */
    ok = ok && rd(f, &hard_pce->s_cyclecount,    4);
    ok = ok && rd(f, &hard_pce->s_cyclecountold,  4);
    ok = ok && rd(f, &cycles_,                    4);
    ok = ok && rd(f, &hard_pce->s_scanline,       4);

    /* MMR */
    ok = ok && rd(f, hard_pce->mmr, 8);

    /* RAM, WRAM, VRAM */
    ok = ok && rd(f, hard_pce->RAM,  0x8000);
    ok = ok && rd(f, hard_pce->WRAM, 0x2000);
    ok = ok && rd(f, hard_pce->VRAM, VRAMSIZE);

    /* SPRAM, Pal */
    ok = ok && rd(f, hard_pce->SPRAM, 64 * 4 * sizeof(uint16));
    ok = ok && rd(f, hard_pce->Pal,   512);

    /* vchange / vchanges */
    ok = ok && rd(f, hard_pce->vchange,  VRAMSIZE / 32);
    ok = ok && rd(f, hard_pce->vchanges, VRAMSIZE / 128);

    /* IO: VCE palette */
    ok = ok && rd(f, hard_pce->s_io.VCE, 0x200 * sizeof(pair));

    /* IO: VCE register + ratch */
    ok = ok && rd(f, &hard_pce->s_io.vce_reg,   sizeof(pair));
    ok = ok && rd(f, &hard_pce->s_io.vce_ratch, 1);

    /* IO: VDC registers (array in io struct — kept for compat) */
    ok = ok && rd(f, hard_pce->s_io.VDC, 32 * sizeof(pair));

    /* IO: VDC standalone vars (MY_VDC_VARS) */
    ok = ok && rd(f, &IO_VDC_00_MAWR,  sizeof(pair));
    ok = ok && rd(f, &IO_VDC_01_MARR,  sizeof(pair));
    ok = ok && rd(f, &IO_VDC_02_VWR,   sizeof(pair));
    ok = ok && rd(f, &IO_VDC_03_vdc3,  sizeof(pair));
    ok = ok && rd(f, &IO_VDC_04_vdc4,  sizeof(pair));
    ok = ok && rd(f, &IO_VDC_05_CR,    sizeof(pair));
    ok = ok && rd(f, &IO_VDC_06_RCR,   sizeof(pair));
    ok = ok && rd(f, &IO_VDC_07_BXR,   sizeof(pair));
    ok = ok && rd(f, &IO_VDC_08_BYR,   sizeof(pair));
    ok = ok && rd(f, &IO_VDC_09_MWR,   sizeof(pair));
    ok = ok && rd(f, &IO_VDC_0A_HSR,   sizeof(pair));
    ok = ok && rd(f, &IO_VDC_0B_HDR,   sizeof(pair));
    ok = ok && rd(f, &IO_VDC_0C_VPR,   sizeof(pair));
    ok = ok && rd(f, &IO_VDC_0D_VDW,   sizeof(pair));
    ok = ok && rd(f, &IO_VDC_0E_VCR,   sizeof(pair));
    ok = ok && rd(f, &IO_VDC_0F_DCR,   sizeof(pair));
    ok = ok && rd(f, &IO_VDC_10_SOUR,  sizeof(pair));
    ok = ok && rd(f, &IO_VDC_11_DISTR, sizeof(pair));
    ok = ok && rd(f, &IO_VDC_12_LENR,  sizeof(pair));
    ok = ok && rd(f, &IO_VDC_13_SATB,  sizeof(pair));
    ok = ok && rd(f, &IO_VDC_14,       sizeof(pair));
    ok = ok && rd(f, &hard_pce->s_io.vdc_inc,          2);
    ok = ok && rd(f, &hard_pce->s_io.vdc_raster_count, 2);
    ok = ok && rd(f, &hard_pce->s_io.vdc_reg,          1);
    ok = ok && rd(f, &hard_pce->s_io.vdc_status,       1);
    ok = ok && rd(f, &hard_pce->s_io.vdc_ratch,        1);
    ok = ok && rd(f, &hard_pce->s_io.vdc_satb,         1);
    ok = ok && rd(f, &hard_pce->s_io.vdc_pendvsync,    1);
    ok = ok && rd(f, &hard_pce->s_io.bg_h,             4);
    ok = ok && rd(f, &hard_pce->s_io.bg_w,             4);
    ok = ok && rd(f, &hard_pce->s_io.screen_w,         4);
    ok = ok && rd(f, &hard_pce->s_io.screen_h,         4);
    ok = ok && rd(f, &hard_pce->s_io.scroll_y,         4);
    ok = ok && rd(f, &hard_pce->s_io.minline,          4);
    ok = ok && rd(f, &hard_pce->s_io.maxline,          4);
    ok = ok && rd(f, &hard_pce->s_io.vdc_min_display,  2);
    ok = ok && rd(f, &hard_pce->s_io.vdc_max_display,  2);

    /* IO: Joypad */
    ok = ok && rd(f, hard_pce->s_io.JOY, 16);
    ok = ok && rd(f, &hard_pce->s_io.joy_select,  1);
    ok = ok && rd(f, &hard_pce->s_io.joy_counter, 1);

    /* IO: PSG */
    ok = ok && rd(f, hard_pce->s_io.PSG,  sizeof(hard_pce->s_io.PSG));
    ok = ok && rd(f, hard_pce->s_io.wave, sizeof(hard_pce->s_io.wave));
    ok = ok && rd(f, &hard_pce->s_io.psg_ch,        1);
    ok = ok && rd(f, &hard_pce->s_io.psg_volume,    1);
    ok = ok && rd(f, &hard_pce->s_io.psg_lfo_freq,  1);
    ok = ok && rd(f, &hard_pce->s_io.psg_lfo_ctrl,  1);
    for (int i = 0; i < 6; i++)
        ok = ok && rd(f, hard_pce->s_io.psg_da_data[i], PSG_DIRECT_ACCESS_BUFSIZE);
    ok = ok && rd(f, hard_pce->s_io.psg_da_index, sizeof(hard_pce->s_io.psg_da_index));
    ok = ok && rd(f, hard_pce->s_io.psg_da_count, sizeof(hard_pce->s_io.psg_da_count));

    /* IO: Timer, IRQ */
    ok = ok && rd(f, &hard_pce->s_io.timer_reload,  1);
    ok = ok && rd(f, &hard_pce->s_io.timer_start,   1);
    ok = ok && rd(f, &hard_pce->s_io.timer_counter, 1);
    ok = ok && rd(f, &hard_pce->s_io.irq_mask,      1);
    ok = ok && rd(f, &hard_pce->s_io.irq_status,    1);

    /* IO: io_buffer */
    ok = ok && rd(f, &hard_pce->s_io.io_buffer, 1);

    /* GFX context */
    ok = ok && rd(f, saved_gfx_context,
                  sizeof(gfx_context) * MAX_GFX_CONTEXT_SLOT_NUMBER);
    ok = ok && rd(f, &gfx_need_redraw, sizeof(gfx_need_redraw));

    fclose(f);

    if (!ok) {
        printf("PCE LoadState: read error\n");
        return false;
    }

    /* Restore bank mappings from MMR values */
    for (int i = 0; i < 8; i++)
        bank_set(i, hard_pce->mmr[i]);

    /* Restore IO_VDC_active_ref from saved vdc_reg */
    IO_VDC_active_set(io.vdc_reg);

    /* Derive bg_w / bg_h from restored MWR register */
    {
        static const uchar bgw[] = { 32, 64, 128, 128 };
        uchar mwr_lo = IO_VDC_09_MWR.B.l;
        io.bg_h = (mwr_lo & 0x40) ? 64 : 32;
        io.bg_w = bgw[(mwr_lo >> 4) & 3];
    }

    /* Derive vdc_inc from restored CR high byte */
    {
        static const uchar incsize[] = { 1, 32, 64, 128 };
        io.vdc_inc = incsize[(IO_VDC_05_CR.B.h >> 3) & 3];
    }

    /* Derive screen_w from restored HDR register */
    io.screen_w = (IO_VDC_0B_HDR.B.l + 1) * 8;

    /* Push the cached VDC control register back into the standalone var.
       The render loop uses saved_gfx_context[0].cr as the active CR. */
    IO_VDC_05_CR.W = saved_gfx_context[0].cr;

    /* Trigger video mode recalc on next frame */
    gfx_need_video_mode_change = 1;

    /* Force full tile-cache rebuild: mark every tile as changed so
       the engine regenerates VRAM2 (decoded tiles) from restored VRAM */
    memset(hard_pce->vchange,  1, VRAMSIZE / 32);
    memset(hard_pce->vchanges, 1, VRAMSIZE / 128);

    /* Rebuild palette */
    SetPalette();

    /* Protect the restored CR value against the game's VBLANK code
       that may zero VDC5 before the display area on the first few frames. */
    pce_resume_cr_value   = saved_gfx_context[0].cr;
    pce_resume_cr_protect = 120;   /* ~2 seconds at 60fps */

    printf("PCE LoadState: OK — PC=0x%04x A=0x%02x VDC05_CR=0x%04x ctx_cr=0x%04x scanline=%lu "
           "bg_w=%d bg_h=%d screen_w=%d screen_h=%d MWR=0x%04x\n",
           (unsigned)reg_pc, (unsigned)reg_a,
           (unsigned)IO_VDC_05_CR.W, (unsigned)saved_gfx_context[0].cr,
           (unsigned long)scanline,
           io.bg_w, io.bg_h, io.screen_w, io.screen_h,
           (unsigned)IO_VDC_09_MWR.W);
    return ok;
}

/* ────────────────────────────────────────────────────────────────── */
/*  Main entry — called from the app's main()                         */
/* ────────────────────────────────────────────────────────────────── */
void huexpress_run(const char *rom_path)
{
    printf("=== PCEngine (HuExpress) start: %s ===\n", rom_path);
    pce_quit_flag = false;
    ignoreMenuButton = true;
    menuButtonFrameCount = 0;
    memset(&previousJoystickState, 0, sizeof(previousJoystickState));

    /* ── Allocate path buffers ── */
    cart_name           = my_special_alloc(0, 1, PATH_MAX_MY);
    short_cart_name     = my_special_alloc(0, 1, PATH_MAX_MY);
    short_iso_name      = my_special_alloc(0, 1, PATH_MAX_MY);
    rom_file_name       = my_special_alloc(0, 1, PATH_MAX_MY);
    config_basepath     = my_special_alloc(0, 1, PATH_MAX_MY);
    sav_path            = my_special_alloc(0, 1, PATH_MAX_MY);
    sav_basepath        = my_special_alloc(0, 1, PATH_MAX_MY);
    tmp_basepath        = my_special_alloc(0, 1, PATH_MAX_MY);
    video_path          = my_special_alloc(0, 1, PATH_MAX_MY);
    ISO_filename        = my_special_alloc(0, 1, PATH_MAX_MY);
    syscard_filename    = my_special_alloc(0, 1, PATH_MAX_MY);
    cdsystem_path       = my_special_alloc(0, 1, PATH_MAX_MY);
    log_filename        = my_special_alloc(0, 1, PATH_MAX_MY);

    strcpy(cart_name, "");
    strcpy(short_cart_name, "");
    strcpy(rom_file_name, "");
    strcpy(config_basepath, "/sd/odroid/data/pce");
    strcpy(tmp_basepath, "");
    strcpy(video_path, "");
    strcpy(ISO_filename, "");
    strcpy(sav_basepath, "/sd/odroid/data");
    strcpy(sav_path, "pce");

    /* ── Sprite priority map ── */
    spr_init_pos = (uint32 *)my_special_alloc(0, 4, 1024 * 4);
    SPM_raw      = (uchar *)my_special_alloc(0, 1, XBUF_WIDTH * XBUF_HEIGHT);
    SPM          = SPM_raw + XBUF_WIDTH * 64 + 32;
    memset(SPM_raw, 0, XBUF_WIDTH * XBUF_HEIGHT);

    /* ── Double-buffered framebuffers (8-bit indexed, XBUF_WIDTH × XBUF_HEIGHT) ── */
    framebuffer[0] = my_special_alloc(0, 1, XBUF_WIDTH * XBUF_HEIGHT);
    framebuffer[1] = my_special_alloc(0, 1, XBUF_WIDTH * XBUF_HEIGHT);
    memset(framebuffer[0], 0, XBUF_WIDTH * XBUF_HEIGHT);
    memset(framebuffer[1], 0, XBUF_WIDTH * XBUF_HEIGHT);

    current_framebuffer = 0;
    XBuf = framebuffer[0];
    osd_gfx_buffer = XBuf + 32 + 64 * XBUF_WIDTH;

    /* ── Double-buffered centred display buffers (8-bit indexed, 320×240) ── */
    for (int i = 0; i < PCE_DISP_BUFS; i++) {
        disp_fb[i] = my_special_alloc(0, 1, PCE_FB_WIDTH * PCE_FB_HEIGHT);
        memset(disp_fb[i], 0, PCE_FB_WIDTH * PCE_FB_HEIGHT);
    }
    disp_fb_idx = 0;

    /* Palette snapshot in internal SRAM for fast lookup (only 512 bytes) */
    disp_pal_snap = heap_caps_malloc(256 * sizeof(uint16_t),
                                     MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!disp_pal_snap) {
        disp_pal_snap = my_special_alloc(0, 1, 256 * sizeof(uint16_t));
    }
    memset(disp_pal_snap, 0, 256 * sizeof(uint16_t));

    /* ── Palette (256 entries RGB565) ── */
    my_palette = my_special_alloc(0, 1, 256 * sizeof(uint16_t));
    memset(my_palette, 0, 256 * sizeof(uint16_t));

    /* ── Audio buffers ── */
    host.sound.stereo       = 1;
    host.sound.signed_sound = 0;
    host.sound.freq         = AUDIO_SAMPLE_RATE;
    host.sound.sample_size  = 1;

    for (int i = 0; i < AUDIO_CHANNELS; i++) {
        sbuf[i] = my_special_alloc(0, 1, AUDIO_BUFFER_SIZE / 2);
    }
    sbuf_mix[0] = my_special_alloc(0, 1, AUDIO_BUFFER_SIZE * 2);
    sbuf_mix[1] = my_special_alloc(0, 1, AUDIO_BUFFER_SIZE * 2);
    audioQueue = xQueueCreate(1, sizeof(void *));

    /* ── Ensure save data directory exists ── */
    mkdir("/sd/odroid/data", 0755);
    mkdir("/sd/odroid/data/pce", 0755);

    /* ── Initialise audio codec ── */
    odroid_audio_init(AUDIO_SAMPLE_RATE);

    /* ── Init PC Engine & load ROM ── */
    printf("PCE: InitPCE('%s')\n", rom_path);
    InitPCE((char *)rom_path);
    osd_init_machine();

    /* Build the 256-entry indexed → RGB565 palette lookup table.
     * pce_gfx_init() is never called by the engine (gfx_init() is a macro
     * that only resets counters), so we must call SetPalette() explicitly. */
    SetPalette();
    printf("PCE: SetPalette done — my_palette[1]=0x%04x my_palette[0x80]=0x%04x\n",
           my_palette[1], my_palette[0x80]);

    /* ── Start audio task ── */
    start_audio();

    /* ── Start async display task ── */
    disp_ready_sem = xSemaphoreCreateBinary();
    disp_go_sem    = xSemaphoreCreateBinary();
    xSemaphoreGive(disp_ready_sem);   /* display is "ready" initially */
    xTaskCreatePinnedToCore(pce_display_task, "pceDisp", 4096, NULL, 6,
                            &disp_task_handle, 1);

    /* ── Run the emulation ── */
    if (odroid_settings_StartAction_get() == ODROID_START_ACTION_RESTART) {
        odroid_settings_StartAction_set(ODROID_START_ACTION_NORMAL);
        pce_post_gfx_init_cb = pce_resume_load_cb;
        printf("PCE: resume load scheduled (post-gfx_init)\n");
    }

    printf("PCE: RunPCE()\n");
    RunPCE();

    /* ── Auto-save on exit ── */
    printf("PCE: Saving state on exit\n");
    PCE_SaveState();

    /* ── Cleanup ── */
    printf("PCE: Cleanup\n");
    disp_task_running = false;
    xSemaphoreGive(disp_go_sem);   /* unblock display task so it can exit */
    vTaskDelay(pdMS_TO_TICKS(150));
    disp_task_handle = NULL;
    if (disp_ready_sem) { vSemaphoreDelete(disp_ready_sem); disp_ready_sem = NULL; }
    if (disp_go_sem)    { vSemaphoreDelete(disp_go_sem);    disp_go_sem    = NULL; }
    stop_audio();
    odroid_audio_terminate();

    /* Free audio buffers */
    for (int i = 0; i < AUDIO_CHANNELS; i++) {
        if (sbuf[i]) { free(sbuf[i]); sbuf[i] = NULL; }
    }
    free(sbuf_mix[0]); sbuf_mix[0] = NULL;
    free(sbuf_mix[1]); sbuf_mix[1] = NULL;
    if (audioQueue) { vQueueDelete(audioQueue); audioQueue = NULL; }

    /* Free display buffers */
    for (int i = 0; i < PCE_DISP_BUFS; i++) {
        if (disp_fb[i]) { free(disp_fb[i]); disp_fb[i] = NULL; }
    }
    if (disp_pal_snap) { free(disp_pal_snap); disp_pal_snap = NULL; }
    if (my_palette)  { free(my_palette);  my_palette  = NULL; }
    free(framebuffer[0]); framebuffer[0] = NULL;
    free(framebuffer[1]); framebuffer[1] = NULL;

    /* Free path buffers */
    free(cart_name);
    free(short_cart_name);
    free(short_iso_name);
    free(rom_file_name);
    free(config_basepath);
    free(sav_path);
    free(sav_basepath);
    free(tmp_basepath);
    free(video_path);
    free(ISO_filename);
    free(syscard_filename);
    free(cdsystem_path);
    free(log_filename);

    free(SPM_raw); SPM_raw = NULL; SPM = NULL;
    free(spr_init_pos); spr_init_pos = NULL;

    printf("=== PCEngine done ===\n");
}
