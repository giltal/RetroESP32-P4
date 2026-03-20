/*
 * atari800_run.cpp — Atari 800 emulator entry point for ESP32-P4
 *
 * Adapted from the original atari800-odroid-go main.cpp.
 * Uses the app_common + odroid compatibility layer pattern.
 * No frame skip — ESP32-P4 at 360MHz renders every frame.
 */

#include "atari800_run.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cctype>
#include <cerrno>
#include <sys/stat.h>
#include <unistd.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_heap_caps.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_log.h"

/* ---------- atari800 core headers ---------- */
#include "config.h"
#include "libatari800.h"
#include "libatari800_main.h"
#include "atari.h"
#include "akey.h"
#include "input.h"
#include "memory.h"
#include "screen.h"
#include "sound.h"
#include "cartridge.h"
#include "sio.h"
#include "afile.h"
#include "pokey.h"
#include "statesav.h"
#include "libatari800_statesav.h"

/* ---------- Odroid platform (C code) ---------- */
extern "C" {
#include "odroid_settings.h"
#include "odroid_audio.h"
#include "odroid_input.h"
#include "odroid_system.h"
#include "odroid_display.h"
#include "odroid_sdcard.h"
}

/* ---------- Constants ---------- */
static const char *TAG = "a800";

#define ATARI_SCREEN_WIDTH   384
#define ATARI_SCREEN_HEIGHT  240
#define DISPLAY_WIDTH        320
#define DISPLAY_HEIGHT       240
#define SRC_X_START          32   /* (384-320)/2 = crop 32px each side */

#define AUDIO_SAMPLE_RATE    15720

/* Joystick masks */
#define JOY_STICK_FORWARD  0x01
#define JOY_STICK_BACK     0x02
#define JOY_STICK_LEFT     0x04
#define JOY_STICK_RIGHT    0x08

/* ---------- Globals ---------- */
static QueueHandle_t vidQueue;
static TaskHandle_t videoTaskHandle;
static uint8_t *framebuffer[2];
static int fb_index = 0;
static uint16_t rgb565_palette[256];
static int16_t *sampleBuffer[2];
static int snd_buf_idx = 0;

/* Async audio task */
static QueueHandle_t audioQueue;
static TaskHandle_t audioTaskHandle;
static volatile bool audioTaskIsRunning = false;

static int _joy[4]  = {0, 0, 0, 0};
static int _trig[4] = {0, 0, 0, 0};
static int console_keys = 7;

/* Frame rendering control — no frame skip on P4 */
int atari800_draw_frame = 1;

/* Sound_desired must be provided (sound.cpp extern) */
Sound_setup_t Sound_desired = {
    AUDIO_SAMPLE_RATE,
    1,  /* 8-bit */
    1,  /* mono */
    0,  /* buffer_ms */
    0   /* buffer_frames */
};

/* Paddle software emulation */
unsigned char AtariPot = 228;
static bool paddle_adc_enabled = false;

/* Paddle ADC constants */
#define PADDLE_DEAD_ZONE    4    /* POT units — small hysteresis */
#define PADDLE_POT_MIN      1
#define PADDLE_POT_MAX      228
#define PADDLE_ADC_LO       200  /* ADC clamp: match pot physical min */
#define PADDLE_ADC_HI       3500 /* ADC clamp: match pot physical max */
#define PADDLE_DETECT_SPREAD 300 /* max ADC spread for pot detection */

/* Platform stubs */
int PLATFORM_kbd_joy_0_enabled = 0;
int PLATFORM_kbd_joy_1_enabled = 0;

/* External references from libatari800 core */
extern int libatari800_init(int argc, char **argv);
extern int libatari800_next_frame(input_template_t *input);
extern ULONG *Screen_atari;
extern UBYTE *MEMORY_mem;
extern UBYTE *under_atarixl_os;
extern UBYTE *under_cart809F;
extern UBYTE *under_cartA0BF;
extern int Atari800_machine_type;
extern int INPUT_key_code;
extern int INPUT_key_consol;
void Sound_Callback(UBYTE *buffer, unsigned int size);
void CARTRIDGE_Remove(void);
void CASSETTE_Remove(void);

/* Double-buffer for non-blocking video */
static uint8_t *lcdfb[2] = { NULL, NULL };
static int lcdfb_write_idx = 0;

/* Save state */
/* File-based save/load via StateSav_SaveAtariState / StateSav_ReadAtariState */
static char save_path[256] = "";
#define STATESAV_MAX_SIZE (210 * 1024)

static volatile bool a800_quit_flag = false;

/* ─── 5×7 Bitmap Font (same as prosystem) ─────────────────────── */
static const uint8_t a800_font5x7[][7] = {
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00}, /* ' ' */
    {0x0E,0x11,0x11,0x1F,0x11,0x11,0x11}, /* A */
    {0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E}, /* B */
    {0x0E,0x11,0x10,0x10,0x10,0x11,0x0E}, /* C */
    {0x1E,0x11,0x11,0x11,0x11,0x11,0x1E}, /* D */
    {0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F}, /* E */
    {0x1F,0x10,0x10,0x1E,0x10,0x10,0x10}, /* F */
    {0x0E,0x11,0x10,0x17,0x11,0x11,0x0E}, /* G */
    {0x11,0x11,0x11,0x1F,0x11,0x11,0x11}, /* H */
    {0x0E,0x04,0x04,0x04,0x04,0x04,0x0E}, /* I */
    {0x07,0x02,0x02,0x02,0x02,0x12,0x0C}, /* J */
    {0x11,0x12,0x14,0x18,0x14,0x12,0x11}, /* K */
    {0x10,0x10,0x10,0x10,0x10,0x10,0x1F}, /* L */
    {0x11,0x1B,0x15,0x15,0x11,0x11,0x11}, /* M */
    {0x11,0x19,0x15,0x13,0x11,0x11,0x11}, /* N */
    {0x0E,0x11,0x11,0x11,0x11,0x11,0x0E}, /* O */
    {0x1E,0x11,0x11,0x1E,0x10,0x10,0x10}, /* P */
    {0x0E,0x11,0x11,0x11,0x15,0x12,0x0D}, /* Q */
    {0x1E,0x11,0x11,0x1E,0x14,0x12,0x11}, /* R */
    {0x0E,0x11,0x10,0x0E,0x01,0x11,0x0E}, /* S */
    {0x1F,0x04,0x04,0x04,0x04,0x04,0x04}, /* T */
    {0x11,0x11,0x11,0x11,0x11,0x11,0x0E}, /* U */
    {0x11,0x11,0x11,0x11,0x11,0x0A,0x04}, /* V */
    {0x11,0x11,0x11,0x15,0x15,0x1B,0x11}, /* W */
    {0x11,0x11,0x0A,0x04,0x0A,0x11,0x11}, /* X */
    {0x11,0x11,0x0A,0x04,0x04,0x04,0x04}, /* Y */
    {0x1F,0x01,0x02,0x04,0x08,0x10,0x1F}, /* Z */
    {0x04,0x04,0x04,0x04,0x04,0x00,0x04}, /* ! */
    {0x10,0x08,0x04,0x02,0x04,0x08,0x10}, /* > */
    {0x00,0x00,0x00,0x00,0x00,0x00,0x04}, /* . */
    {0x00,0x00,0x00,0x00,0x00,0x04,0x08}, /* , */
    {0x00,0x04,0x00,0x00,0x00,0x04,0x00}, /* : */
    {0x18,0x19,0x02,0x04,0x08,0x13,0x03}, /* % */
    {0x0E,0x11,0x13,0x15,0x19,0x11,0x0E}, /* 0 */
    {0x04,0x0C,0x04,0x04,0x04,0x04,0x0E}, /* 1 */
    {0x0E,0x11,0x01,0x02,0x04,0x08,0x1F}, /* 2 */
    {0x0E,0x11,0x01,0x06,0x01,0x11,0x0E}, /* 3 */
    {0x02,0x06,0x0A,0x12,0x1F,0x02,0x02}, /* 4 */
    {0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E}, /* 5 */
    {0x06,0x08,0x10,0x1E,0x11,0x11,0x0E}, /* 6 */
    {0x1F,0x01,0x02,0x04,0x08,0x08,0x08}, /* 7 */
    {0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E}, /* 8 */
    {0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C}, /* 9 */
    /* a-z lowercase */
    {0x00,0x00,0x0E,0x01,0x0F,0x11,0x0F},
    {0x10,0x10,0x1E,0x11,0x11,0x11,0x1E},
    {0x00,0x00,0x0E,0x11,0x10,0x11,0x0E},
    {0x01,0x01,0x0F,0x11,0x11,0x11,0x0F},
    {0x00,0x00,0x0E,0x11,0x1F,0x10,0x0E},
    {0x06,0x08,0x08,0x1C,0x08,0x08,0x08},
    {0x00,0x00,0x0F,0x11,0x0F,0x01,0x0E},
    {0x10,0x10,0x1E,0x11,0x11,0x11,0x11},
    {0x04,0x00,0x0C,0x04,0x04,0x04,0x0E},
    {0x02,0x00,0x06,0x02,0x02,0x12,0x0C},
    {0x10,0x10,0x12,0x14,0x18,0x14,0x12},
    {0x0C,0x04,0x04,0x04,0x04,0x04,0x0E},
    {0x00,0x00,0x1A,0x15,0x15,0x15,0x15},
    {0x00,0x00,0x1E,0x11,0x11,0x11,0x11},
    {0x00,0x00,0x0E,0x11,0x11,0x11,0x0E},
    {0x00,0x00,0x1E,0x11,0x1E,0x10,0x10},
    {0x00,0x00,0x0F,0x11,0x0F,0x01,0x01},
    {0x00,0x00,0x16,0x19,0x10,0x10,0x10},
    {0x00,0x00,0x0F,0x10,0x0E,0x01,0x1E},
    {0x08,0x08,0x1C,0x08,0x08,0x09,0x06},
    {0x00,0x00,0x11,0x11,0x11,0x11,0x0F},
    {0x00,0x00,0x11,0x11,0x11,0x0A,0x04},
    {0x00,0x00,0x11,0x11,0x15,0x15,0x0A},
    {0x00,0x00,0x11,0x0A,0x04,0x0A,0x11},
    {0x00,0x00,0x11,0x11,0x0F,0x01,0x0E},
    {0x00,0x00,0x1F,0x02,0x04,0x08,0x1F},
};

static int a800_font_index(char c)
{
    if (c == ' ')  return 0;
    if (c >= 'A' && c <= 'Z') return 1 + (c - 'A');
    if (c == '!')  return 27;
    if (c == '>')  return 28;
    if (c == '.')  return 29;
    if (c == ',')  return 30;
    if (c == ':')  return 31;
    if (c == '%')  return 32;
    if (c >= '0' && c <= '9') return 33 + (c - '0');
    if (c >= 'a' && c <= 'z') return 43 + (c - 'a');
    return 0;
}

#define A800_COLOR_BLACK   0x0000
#define A800_COLOR_WHITE   0xFFFF
#define A800_COLOR_YELLOW  0xE0FF
#define A800_COLOR_DKGRAY  0x2108
#define A800_COLOR_GREEN   0xE007
#define A800_COLOR_CYAN    0xFF07

static void a800_draw_char(uint16_t *fb, int px, int py, char c, uint16_t color)
{
    int idx = a800_font_index(c);
    const uint8_t *glyph = a800_font5x7[idx];
    for (int row = 0; row < 7; row++) {
        int yy = py + row;
        if (yy < 0 || yy >= DISPLAY_HEIGHT) continue;
        uint8_t bits = glyph[row];
        for (int col = 0; col < 5; col++) {
            int xx = px + col;
            if (xx < 0 || xx >= DISPLAY_WIDTH) continue;
            if (bits & (0x10 >> col))
                fb[yy * DISPLAY_WIDTH + xx] = color;
        }
    }
}

static void a800_draw_string(uint16_t *fb, int px, int py, const char *str, uint16_t color)
{
    while (*str) {
        a800_draw_char(fb, px, py, *str, color);
        px += 6;
        str++;
    }
}

static void a800_fill_rect(uint16_t *fb, int x, int y, int w, int h, uint16_t color)
{
    for (int row = y; row < y + h && row < DISPLAY_HEIGHT; row++) {
        if (row < 0) continue;
        for (int col = x; col < x + w && col < DISPLAY_WIDTH; col++) {
            if (col < 0) continue;
            fb[row * DISPLAY_WIDTH + col] = color;
        }
    }
}

/* ─── RGB565 palette ──────────────────────────────────────────── */
static const uint32_t atari_palette_rgb[256] = {
    0x000000,0x111111,0x222222,0x333333,0x444444,0x555555,0x666666,0x777777,
    0x888888,0x999999,0xaaaaaa,0xbbbbbb,0xcccccc,0xdddddd,0xeeeeee,0xffffff,
    0x190700,0x2a1800,0x3b2900,0x4c3a00,0x5d4b00,0x6e5c00,0x7f6d00,0x907e09,
    0xa18f1a,0xb3a02b,0xc3b13c,0xd4c24d,0xe5d35e,0xf7e46f,0xfff582,0xffff96,
    0x310000,0x3f0000,0x531700,0x642800,0x753900,0x864a00,0x975b0a,0xa86c1b,
    0xb97d2c,0xca8e3d,0xdb9f4e,0xecb05f,0xfdc170,0xffd285,0xffe39c,0xfff4b2,
    0x420404,0x4f0000,0x600800,0x711900,0x822a0d,0x933b1e,0xa44c2f,0xb55d40,
    0xc66e51,0xd77f62,0xe89073,0xf9a183,0xffb298,0xffc3ae,0xffd4c4,0xffe5da,
    0x410103,0x50000f,0x61001b,0x720f2b,0x83203c,0x94314d,0xa5425e,0xb6536f,
    0xc76480,0xd87591,0xe986a2,0xfa97b3,0xffa8c8,0xffb9de,0xffcaef,0xfbdcf6,
    0x330035,0x440041,0x55004c,0x660c5c,0x771d6d,0x882e7e,0x993f8f,0xaa50a0,
    0xbb61b1,0xcc72c2,0xdd83d3,0xee94e4,0xffa5e4,0xffb6e9,0xffc7ee,0xffd8f3,
    0x1d005c,0x2e0068,0x400074,0x511084,0x622195,0x7332a6,0x8443b7,0x9554c8,
    0xa665d9,0xb776ea,0xc887eb,0xd998eb,0xe9a9ec,0xfbbaeb,0xffcbef,0xffdff9,
    0x020071,0x13007d,0x240b8c,0x351c9d,0x462dae,0x573ebf,0x684fd0,0x7960e1,
    0x8a71f2,0x9b82f7,0xac93f7,0xbda4f7,0xceb5f7,0xdfc6f7,0xf0d7f7,0xffe8f8,
    0x000068,0x000a7c,0x081b90,0x192ca1,0x2a3db2,0x3b4ec3,0x4c5fd4,0x5d70e5,
    0x6e81f6,0x7f92ff,0x90a3ff,0xa1b4ff,0xb2c5ff,0xc3d6ff,0xd4e7ff,0xe5f8ff,
    0x000a4d,0x001b63,0x002c79,0x023d8f,0x134ea0,0x245fb1,0x3570c2,0x4681d3,
    0x5792e4,0x68a3f5,0x79b4ff,0x8ac5ff,0x9bd6ff,0xace7ff,0xbdf8ff,0xceffff,
    0x001a26,0x002b3c,0x003c52,0x004d68,0x065e7c,0x176f8d,0x28809e,0x3991af,
    0x4aa2c0,0x5bb3d1,0x6cc4e2,0x7dd5f3,0x8ee6ff,0x9ff7ff,0xb0ffff,0xc1ffff,
    0x01250a,0x023610,0x004622,0x005738,0x05684d,0x16795e,0x278a6f,0x389b80,
    0x49ac91,0x5abda2,0x6bceb3,0x7cdfc4,0x8df0d5,0x9effe5,0xaffff1,0xc0fffd,
    0x04260d,0x043811,0x054713,0x005a1b,0x106b1b,0x217c2c,0x328d3d,0x439e4e,
    0x54af5f,0x65c070,0x76d181,0x87e292,0x98f3a3,0xa9ffb3,0xbaffbf,0xcbffcb,
    0x00230a,0x003510,0x044613,0x155613,0x266713,0x377813,0x488914,0x599a25,
    0x6aab36,0x7bbc47,0x8ccd58,0x9dde69,0xaeef7a,0xbfff8b,0xd0ff97,0xe1ffa3,
    0x001707,0x0e2808,0x1f3908,0x304a08,0x415b08,0x526c08,0x637d08,0x748e0d,
    0x859f1e,0x96b02f,0xa7c140,0xb8d251,0xc9e362,0xdaf473,0xebff82,0xfcff8e,
    0x1b0701,0x2c1801,0x3c2900,0x4d3b00,0x5f4c00,0x705e00,0x816f00,0x938009,
    0xa4921a,0xb2a02b,0xc7b43d,0xd8c64e,0xead760,0xf6e46f,0xfffa84,0xffff99,
};

static void build_rgb565_palette(void)
{
    for (int i = 0; i < 256; i++) {
        uint32_t c = atari_palette_rgb[i];
        uint16_t r = (c >> 16) & 0xFF;
        uint16_t g = (c >> 8)  & 0xFF;
        uint16_t b = (c)       & 0xFF;
        rgb565_palette[i] = ((r << 8) & 0xF800) | ((g << 3) & 0x07E0) | (b >> 3);
    }
}

/* ─── Video task ──────────────────────────────────────────────── */
static volatile bool videoTaskIsRunning = false;
static uint16_t *s_vid_rgb565 = NULL;  /* 320×240 RGB565 temp buffer for PPA */

static void videoTask(void *arg)
{
    uint8_t *param;
    videoTaskIsRunning = true;

    /* Allocate DMA-aligned 320×240 RGB565 temp buffer for direct PPA path */
    if (!s_vid_rgb565) {
        s_vid_rgb565 = (uint16_t *)heap_caps_aligned_calloc(
            64, 1, DISPLAY_WIDTH * DISPLAY_HEIGHT * sizeof(uint16_t),
            MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA);
    }

    while (1) {
        xQueuePeek(vidQueue, &param, portMAX_DELAY);

        if ((uintptr_t)param == 1)
            break;

        /* Palette convert 320×240 8-bit indexed → RGB565 */
        const uint32_t *in32  = (const uint32_t *)param;
        uint32_t       *out32 = (uint32_t *)s_vid_rgb565;
        int count = (DISPLAY_WIDTH * DISPLAY_HEIGHT) / 4;
        for (int i = 0; i < count; i++) {
            uint32_t pix4 = in32[i];
            uint16_t p0 = rgb565_palette[(pix4 >>  0) & 0xFF];
            uint16_t p1 = rgb565_palette[(pix4 >>  8) & 0xFF];
            uint16_t p2 = rgb565_palette[(pix4 >> 16) & 0xFF];
            uint16_t p3 = rgb565_palette[(pix4 >> 24) & 0xFF];
            out32[i * 2]     = p0 | ((uint32_t)p1 << 16);
            out32[i * 2 + 1] = p2 | ((uint32_t)p3 << 16);
        }

        /* Direct PPA path: 320×240 → 2× scale + 270° rotate → 480×640 */
        ili9341_write_frame_rgb565_ex(s_vid_rgb565, false);

        xQueueReceive(vidQueue, &param, portMAX_DELAY);
    }

    videoTaskIsRunning = false;
    vTaskDelete(NULL);
    while (1) {}
}

/* ─── Audio task (async I2S submission) ───────────────────────── */
typedef struct {
    int16_t *buf;
    int      samples;
} audio_msg_t;

static void audioTask(void *arg)
{
    audio_msg_t msg;
    audioTaskIsRunning = true;

    while (1) {
        xQueueReceive(audioQueue, &msg, portMAX_DELAY);
        if (msg.buf == NULL) break;          /* poison pill = stop */
        odroid_audio_submit(msg.buf, msg.samples);
    }

    audioTaskIsRunning = false;
    vTaskDelete(NULL);
    while (1) {}
}

/* ─── Platform interface (required by libatari800 core) ─────── */
int PLATFORM_Keyboard(void)
{
    return INPUT_key_code;
}

int PLATFORM_PORT(int num)
{
    if (num == 0)
        return (_joy[0] | (_joy[1] << 4)) ^ 0xFF;
    if (num == 1)
        return (_joy[2] | (_joy[3] << 4)) ^ 0xFF;
    return 0xFF;
}

int PLATFORM_TRIG(int num)
{
    if (num < 0 || num >= 4)
        return 1;
    return _trig[num] ^ 1;
}

void LIBATARI800_Mouse(void) { }

int LIBATARI800_Input_Initialise(int *argc, char *argv[])
{
    return TRUE;
}

/* ─── Memory pre-allocation ───────────────────────────────────── */
static void atari800_preallocate(void)
{
    ESP_LOGI(TAG, "Pre-allocating atari800 buffers...");

    /* Screen_atari: 384×240 indexed */
    Screen_atari = (ULONG *)heap_caps_malloc(ATARI_SCREEN_WIDTH * ATARI_SCREEN_HEIGHT,
                                             MALLOC_CAP_SPIRAM);
    if (!Screen_atari) abort();
    memset(Screen_atari, 0, ATARI_SCREEN_WIDTH * ATARI_SCREEN_HEIGHT);

    /* MEMORY_mem: 64KB + 4 (6502 address space) — try internal first */
    MEMORY_mem = (UBYTE *)heap_caps_malloc(65536 + 4,
                                           MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);
    if (!MEMORY_mem)
        MEMORY_mem = (UBYTE *)heap_caps_malloc(65536 + 4, MALLOC_CAP_SPIRAM);
    if (!MEMORY_mem) abort();
    memset(MEMORY_mem, 0, 65536 + 4);

    /* XL/XE underlay buffers */
    under_atarixl_os = (UBYTE *)heap_caps_malloc(16384, MALLOC_CAP_SPIRAM);
    under_cart809F   = (UBYTE *)heap_caps_malloc(8192,  MALLOC_CAP_SPIRAM);
    under_cartA0BF   = (UBYTE *)heap_caps_malloc(8192,  MALLOC_CAP_SPIRAM);
    if (!under_atarixl_os || !under_cart809F || !under_cartA0BF) abort();

    /* Double-buffered display (320×240 indexed) */
    for (int i = 0; i < 2; i++) {
        framebuffer[i] = (uint8_t *)heap_caps_malloc(DISPLAY_WIDTH * DISPLAY_HEIGHT,
                                                      MALLOC_CAP_SPIRAM);
        if (!framebuffer[i]) abort();
        memset(framebuffer[i], 0, DISPLAY_WIDTH * DISPLAY_HEIGHT);
    }

    ESP_LOGI(TAG, "Buffer alloc done. Free heap: %u", (unsigned)esp_get_free_heap_size());
}

/* ─── File extension helpers ──────────────────────────────────── */
static const char *get_ext(const char *path)
{
    const char *dot = NULL;
    const char *p = path;
    while (*p) { if (*p == '.') dot = p + 1; p++; }
    return dot ? dot : "";
}

static int strcicmp_ext(const char *a, const char *b)
{
    while (*a && *b) {
        int d = tolower((unsigned char)*a) - tolower((unsigned char)*b);
        if (d != 0) return d;
        a++; b++;
    }
    return tolower((unsigned char)*a) - tolower((unsigned char)*b);
}

/* ─── Emulator init ───────────────────────────────────────────── */
static void emu_init(const char *filename)
{
    ESP_LOGI(TAG, "emu_init: %s", filename);
    atari800_preallocate();
    build_rgb565_palette();

    const char *ext = get_ext(filename);
    const char *argv[16];
    int argc = 0;
    argv[argc++] = "atari800";

    if (strcicmp_ext(ext, "car") == 0) {
        argv[argc++] = "-xl";
        argv[argc++] = "-nobasic";
        argv[argc++] = (char *)filename;
    }
    else if (strcicmp_ext(ext, "rom") == 0 || strcicmp_ext(ext, "bin") == 0) {
        FILE *fp = fopen(filename, "rb");
        if (fp) {
            fseek(fp, 0, SEEK_END);
            long len = ftell(fp);
            fclose(fp);
            if (len == 0x8000) {
                argv[argc++] = "-5200";
                argv[argc++] = "-cart-type"; argv[argc++] = "4";
                argv[argc++] = "-cart";      argv[argc++] = (char *)filename;
            } else if (len == 0x4000) {
                argv[argc++] = "-atari";
                argv[argc++] = "-cart-type"; argv[argc++] = "2";
                argv[argc++] = "-cart";      argv[argc++] = (char *)filename;
            } else if (len == 0x2000) {
                argv[argc++] = "-atari";
                argv[argc++] = "-cart-type"; argv[argc++] = "1";
                argv[argc++] = "-cart";      argv[argc++] = (char *)filename;
            } else {
                argv[argc++] = "-xl"; argv[argc++] = "-nobasic";
                argv[argc++] = (char *)filename;
            }
        } else {
            argv[argc++] = "-xl"; argv[argc++] = "-nobasic";
            argv[argc++] = (char *)filename;
        }
    }
    else if (strcicmp_ext(ext, "a52") == 0) {
        /* Atari 5200 cartridge image — always 32KB standard, use -5200 with cart-type 4 */
        argv[argc++] = "-5200";
        argv[argc++] = "-cart-type"; argv[argc++] = "4";
        argv[argc++] = "-cart";      argv[argc++] = (char *)filename;
    }
    else if (strcicmp_ext(ext, "cas") == 0) {
        argv[argc++] = "-xl";
        argv[argc++] = "-boottape"; argv[argc++] = (char *)filename;
    }
    else if (strcicmp_ext(ext, "atr") == 0) {
        argv[argc++] = "-xl"; argv[argc++] = "-nobasic";
        argv[argc++] = (char *)filename;
    }
    else {
        /* .xex, .com and anything else */
        argv[argc++] = "-xl"; argv[argc++] = "-nobasic";
        argv[argc++] = (char *)filename;
    }

    argv[argc++] = "-ntsc";

    ESP_LOGI(TAG, "libatari800_init with %d args", argc);
    int result = libatari800_init(argc, (char **)argv);
    ESP_LOGI(TAG, "libatari800_init returned %d", result);

    Sound_desired.freq = AUDIO_SAMPLE_RATE;
}

/* ─── Frame step ──────────────────────────────────────────────── */
static void emu_step(odroid_gamepad_state *gamepad)
{
    /* ─── Paddle input ──────────────────────────────────────────── */
    if (paddle_adc_enabled) {
        /* Analog paddle: EMA-smoothed ADC → POT range [228..1] (inverted: low ADC = left = POT 228) */
        static int ema_adc = -1;
        int paddle_adc = odroid_paddle_adc_raw;
        if (paddle_adc >= 0) {
            if (ema_adc < 0)
                ema_adc = paddle_adc;
            else
                ema_adc = (51 * paddle_adc + 205 * ema_adc + 128) >> 8;

            int clamped = ema_adc;
            if (clamped < PADDLE_ADC_LO) clamped = PADDLE_ADC_LO;
            if (clamped > PADDLE_ADC_HI) clamped = PADDLE_ADC_HI;
            int target_pot = PADDLE_POT_MAX -
                ((clamped - PADDLE_ADC_LO) * (PADDLE_POT_MAX - PADDLE_POT_MIN))
                / (PADDLE_ADC_HI - PADDLE_ADC_LO);

            int diff = target_pot - (int)AtariPot;
            if (diff < 0) diff = -diff;
            if (diff >= PADDLE_DEAD_ZONE)
                AtariPot = (unsigned char)target_pot;
        }
    } else {
        /* Software paddle fallback: D-pad L/R with acceleration */
        static int paddle_hold_frames = 0;
        int dir = 0;
        if (gamepad->values[ODROID_INPUT_LEFT])  dir = -1;
        if (gamepad->values[ODROID_INPUT_RIGHT]) dir =  1;

        if (dir != 0) {
            paddle_hold_frames++;
            int step = (paddle_hold_frames >= 30) ? 6 :
                       (paddle_hold_frames >= 15) ? 3 : 1;
            int pot = (int)AtariPot + dir * step;
            if (pot < PADDLE_POT_MIN) pot = PADDLE_POT_MIN;
            if (pot > PADDLE_POT_MAX) pot = PADDLE_POT_MAX;
            AtariPot = (unsigned char)pot;
        } else {
            paddle_hold_frames = 0;
        }
    }

    /* Map gamepad to Atari joystick */
    _joy[0] = 0;
    _trig[0] = 0;
    if (gamepad->values[ODROID_INPUT_UP])    _joy[0] |= JOY_STICK_FORWARD;
    if (gamepad->values[ODROID_INPUT_DOWN])  _joy[0] |= JOY_STICK_BACK;
    if (gamepad->values[ODROID_INPUT_LEFT])  _joy[0] |= JOY_STICK_LEFT;
    if (gamepad->values[ODROID_INPUT_RIGHT]) _joy[0] |= JOY_STICK_RIGHT;
    if (gamepad->values[ODROID_INPUT_A])     _trig[0] = 1;
    if (gamepad->values[ODROID_INPUT_B])     _trig[0] = 1;

    /* Console keys */
    console_keys = 0x07;
    if (gamepad->values[ODROID_INPUT_START])  console_keys &= ~0x01;
    if (gamepad->values[ODROID_INPUT_SELECT]) console_keys &= ~0x02;
    INPUT_key_consol = console_keys;

    if (Atari800_machine_type == Atari800_MACHINE_5200) {
        INPUT_key_code = AKEY_NONE;
        if (gamepad->values[ODROID_INPUT_START])
            INPUT_key_code = AKEY_5200_START;
    } else {
        INPUT_key_code = AKEY_NONE;
    }

    /* No frame skip — always render */
    atari800_draw_frame = 1;

    /* Run one frame */
    libatari800_next_frame(NULL);

    /* Video: crop 384→320 from Screen_atari into display buffer */
    {
        uint8_t *src = (uint8_t *)Screen_atari;
        uint8_t *dst = framebuffer[fb_index];
        for (int y = 0; y < DISPLAY_HEIGHT; y++) {
            memcpy(dst, src + SRC_X_START, DISPLAY_WIDTH);
            dst += DISPLAY_WIDTH;
            src += ATARI_SCREEN_WIDTH;
        }
        uint8_t *fb_ptr = framebuffer[fb_index];
        xQueueOverwrite(vidQueue, &fb_ptr);
        fb_index ^= 1;
    }

    /* Audio: POKEY at 15720Hz → 262 samples/frame @60fps (async) */
    int samples_per_frame = AUDIO_SAMPLE_RATE / 60;
    if (!sampleBuffer[0]) {
        for (int b = 0; b < 2; b++) {
            sampleBuffer[b] = (int16_t *)heap_caps_malloc(samples_per_frame * 2 * sizeof(int16_t),
                                                           MALLOC_CAP_SPIRAM);
            if (!sampleBuffer[b]) return;
        }
    }

    int16_t *sbuf = sampleBuffer[snd_buf_idx];
    uint8_t audio8[512];
    Sound_Callback((UBYTE *)audio8, samples_per_frame);

    for (int i = 0; i < samples_per_frame; i++) {
        int16_t s16 = ((int)audio8[i] - 128) << 8;
        sbuf[i * 2]     = s16;
        sbuf[i * 2 + 1] = s16;
    }

    /* Send to audio task (non-blocking from emulation perspective) */
    audio_msg_t amsg = { sbuf, samples_per_frame };
    xQueueSend(audioQueue, &amsg, portMAX_DELAY);
    snd_buf_idx ^= 1;
}

/* ─── Save/Load state helpers ─────────────────────────────────── */
static void build_save_path(const char *rom_path)
{
    char *fileName = odroid_util_GetFileName(rom_path);
    if (!fileName) return;
    snprintf(save_path, sizeof(save_path), "/sd/odroid/data/a800/%s.sav", fileName);
    free(fileName);
}

static void SaveState(void)
{
    if (save_path[0] == '\0') return;

    char dirBuf[256];
    snprintf(dirBuf, sizeof(dirBuf), "/sd/odroid");         mkdir(dirBuf, 0775);
    snprintf(dirBuf, sizeof(dirBuf), "/sd/odroid/data");    mkdir(dirBuf, 0775);
    snprintf(dirBuf, sizeof(dirBuf), "/sd/odroid/data/a800"); mkdir(dirBuf, 0775);

    int rc = StateSav_SaveAtariState(save_path, "wb", TRUE);
    if (rc) {
        ESP_LOGI(TAG, "Saved state to %s", save_path);
    } else {
        ESP_LOGW(TAG, "Failed to save state to %s", save_path);
    }
}

static bool LoadState(void)
{
    if (save_path[0] == '\0') return false;

    struct stat st;
    if (stat(save_path, &st) != 0) return false;

    int rc = StateSav_ReadAtariState(save_path, "rb");
    ESP_LOGI(TAG, "Loaded state from %s (rc=%d)", save_path, rc);
    return rc != 0;
}

static bool a800_check_save_exists(void)
{
    if (save_path[0] == '\0') return false;
    struct stat st;
    return (stat(save_path, &st) == 0);
}

static bool a800_delete_save(void)
{
    if (save_path[0] == '\0') return false;
    struct stat st;
    if (stat(save_path, &st) == 0)
        return (unlink(save_path) == 0);
    return false;
}

/* ─── Volume Overlay ──────────────────────────────────────────── */
static void a800_show_volume_overlay(void)
{
    uint16_t *fb = s_vid_rgb565;
    if (!fb) return;

    static const char *level_names[ODROID_VOLUME_LEVEL_COUNT] = {
        "MUTE", "25%", "50%", "75%", "100%"
    };

    int level = (int)odroid_audio_volume_get();
    int timeout = 25;

    odroid_gamepad_state prev;
    odroid_input_gamepad_read(&prev);
    for (int i = 0; i < 100; i++) {
        odroid_gamepad_state tmp;
        odroid_input_gamepad_read(&tmp);
        if (!tmp.values[ODROID_INPUT_VOLUME]) break;
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    odroid_input_gamepad_read(&prev);

    while (timeout > 0) {
        int box_w = 140, box_h = 34;
        int box_x = (DISPLAY_WIDTH - box_w) / 2, box_y = 8;

        a800_fill_rect(fb, box_x, box_y, box_w, box_h, A800_COLOR_BLACK);
        a800_fill_rect(fb, box_x, box_y, box_w, 1, A800_COLOR_WHITE);
        a800_fill_rect(fb, box_x, box_y + box_h - 1, box_w, 1, A800_COLOR_WHITE);
        a800_fill_rect(fb, box_x, box_y, 1, box_h, A800_COLOR_WHITE);
        a800_fill_rect(fb, box_x + box_w - 1, box_y, 1, box_h, A800_COLOR_WHITE);

        char title[32];
        snprintf(title, sizeof(title), "VOLUME: %s", level_names[level]);
        int title_w = strlen(title) * 6;
        a800_draw_string(fb, box_x + (box_w - title_w) / 2, box_y + 4, title, A800_COLOR_YELLOW);

        int bar_x = box_x + 10, bar_y = box_y + 16, bar_w = box_w - 20, bar_h = 10;
        a800_fill_rect(fb, bar_x, bar_y, bar_w, bar_h, A800_COLOR_DKGRAY);
        if (level > 0) {
            int fill_w = (bar_w * level) / (ODROID_VOLUME_LEVEL_COUNT - 1);
            uint16_t bar_color = (level <= 1) ? A800_COLOR_GREEN :
                                 (level <= 3) ? A800_COLOR_CYAN : A800_COLOR_YELLOW;
            a800_fill_rect(fb, bar_x, bar_y, fill_w, bar_h, bar_color);
        }
        a800_fill_rect(fb, bar_x, bar_y, bar_w, 1, A800_COLOR_WHITE);
        a800_fill_rect(fb, bar_x, bar_y + bar_h - 1, bar_w, 1, A800_COLOR_WHITE);
        a800_fill_rect(fb, bar_x, bar_y, 1, bar_h, A800_COLOR_WHITE);
        a800_fill_rect(fb, bar_x + bar_w - 1, bar_y, 1, bar_h, A800_COLOR_WHITE);
        for (int i = 1; i < ODROID_VOLUME_LEVEL_COUNT - 1; i++) {
            int sx = bar_x + (bar_w * i) / (ODROID_VOLUME_LEVEL_COUNT - 1);
            a800_fill_rect(fb, sx, bar_y, 1, bar_h, A800_COLOR_WHITE);
        }

        ili9341_write_frame_rgb565_ex(fb, false);

        vTaskDelay(pdMS_TO_TICKS(80));
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
            (state.values[ODROID_INPUT_B] && !prev.values[ODROID_INPUT_B]))
            timeout = 0;
        if (changed) {
            odroid_audio_volume_set(level);
            odroid_settings_Volume_set(level);
        }
        prev = state;
        timeout--;
    }
}

/* ─── In-Game Menu ─────────────────────────────────────────────── */
#define A800_MENU_RESUME     0
#define A800_MENU_RESTART    1
#define A800_MENU_SAVE       2
#define A800_MENU_RELOAD     3
#define A800_MENU_OVERWRITE  4
#define A800_MENU_DELETE     5
#define A800_MENU_EXIT       6

static bool a800_show_ingame_menu(void)
{
    uint16_t *fb = s_vid_rgb565;
    if (!fb) return true;

    bool has_save = a800_check_save_exists();

    const char *labels[8];
    int ids[8];
    int count = 0;

    labels[count] = "Resume Game";      ids[count] = A800_MENU_RESUME;    count++;
    labels[count] = "Restart Game";     ids[count] = A800_MENU_RESTART;   count++;
    labels[count] = "Save Game";        ids[count] = A800_MENU_SAVE;      count++;
    if (has_save) {
        labels[count] = "Reload Game";  ids[count] = A800_MENU_RELOAD;    count++;
        labels[count] = "Overwrite Save"; ids[count] = A800_MENU_OVERWRITE; count++;
        labels[count] = "Delete Save";  ids[count] = A800_MENU_DELETE;    count++;
    }
    labels[count] = "Exit Game";        ids[count] = A800_MENU_EXIT;      count++;

    int selected = 0;
    odroid_gamepad_state prev;
    odroid_input_gamepad_read(&prev);

    for (int i = 0; i < 200; i++) {
        odroid_gamepad_state tmp;
        odroid_input_gamepad_read(&tmp);
        if (!tmp.values[ODROID_INPUT_MENU]) break;
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    odroid_input_gamepad_read(&prev);

    bool keep_running = true;
    bool menu_active = true;
    const char *flash_msg = NULL;
    int flash_timer = 0;

    while (menu_active) {
        int box_w = 160;
        int box_h = 20 + count * 14 + 10;
        int box_x = (DISPLAY_WIDTH - box_w) / 2;
        int box_y = (DISPLAY_HEIGHT - box_h) / 2;

        a800_fill_rect(fb, box_x, box_y, box_w, box_h, A800_COLOR_BLACK);
        a800_fill_rect(fb, box_x, box_y, box_w, 1, A800_COLOR_WHITE);
        a800_fill_rect(fb, box_x, box_y + box_h - 1, box_w, 1, A800_COLOR_WHITE);
        a800_fill_rect(fb, box_x, box_y, 1, box_h, A800_COLOR_WHITE);
        a800_fill_rect(fb, box_x + box_w - 1, box_y, 1, box_h, A800_COLOR_WHITE);

        a800_draw_string(fb, box_x + (box_w - 9*6)/2, box_y + 5, "GAME MENU", A800_COLOR_YELLOW);

        for (int i = 0; i < count; i++) {
            int oy = box_y + 18 + i * 14;
            int ox = box_x + 16;
            uint16_t color = (i == selected) ? A800_COLOR_YELLOW : A800_COLOR_WHITE;
            a800_fill_rect(fb, box_x + 2, oy - 1, box_w - 4, 10, A800_COLOR_BLACK);
            if (i == selected)
                a800_draw_char(fb, box_x + 6, oy, '>', A800_COLOR_YELLOW);
            a800_draw_string(fb, ox, oy, labels[i], color);
        }

        if (flash_msg && flash_timer > 0) {
            int fw = strlen(flash_msg) * 6;
            int fx = box_x + (box_w - fw) / 2;
            int fy = box_y + box_h - 12;
            a800_fill_rect(fb, box_x + 2, fy - 2, box_w - 4, 12, A800_COLOR_BLACK);
            a800_draw_string(fb, fx, fy, flash_msg, A800_COLOR_GREEN);
            flash_timer--;
            if (flash_timer == 0) flash_msg = NULL;
        }

        ili9341_write_frame_rgb565_ex(fb, false);

        vTaskDelay(pdMS_TO_TICKS(80));
        odroid_gamepad_state state;
        odroid_input_gamepad_read(&state);

        if (state.values[ODROID_INPUT_UP] && !prev.values[ODROID_INPUT_UP])
            selected = (selected - 1 + count) % count;
        if (state.values[ODROID_INPUT_DOWN] && !prev.values[ODROID_INPUT_DOWN])
            selected = (selected + 1) % count;

        if ((state.values[ODROID_INPUT_B] && !prev.values[ODROID_INPUT_B]) ||
            (state.values[ODROID_INPUT_MENU] && !prev.values[ODROID_INPUT_MENU])) {
            menu_active = false;
            keep_running = true;
        }

        if (state.values[ODROID_INPUT_A] && !prev.values[ODROID_INPUT_A]) {
            switch (ids[selected]) {
                case A800_MENU_RESUME:
                    menu_active = false;
                    keep_running = true;
                    break;
                case A800_MENU_RESTART:
                    /* Reinitialize via the core's cold start */
                    Atari800_Coldstart();
                    menu_active = false;
                    keep_running = true;
                    break;
                case A800_MENU_RELOAD:
                    LoadState();
                    flash_msg = "Loaded!";
                    flash_timer = 15;
                    menu_active = false;
                    keep_running = true;
                    break;
                case A800_MENU_SAVE:
                    SaveState();
                    if (!has_save) {
                        has_save = true;
                        count = 0;
                        labels[count] = "Resume Game";      ids[count] = A800_MENU_RESUME;    count++;
                        labels[count] = "Restart Game";     ids[count] = A800_MENU_RESTART;   count++;
                        labels[count] = "Save Game";        ids[count] = A800_MENU_SAVE;      count++;
                        labels[count] = "Reload Game";      ids[count] = A800_MENU_RELOAD;    count++;
                        labels[count] = "Overwrite Save";   ids[count] = A800_MENU_OVERWRITE; count++;
                        labels[count] = "Delete Save";      ids[count] = A800_MENU_DELETE;    count++;
                        labels[count] = "Exit Game";        ids[count] = A800_MENU_EXIT;      count++;
                    }
                    flash_msg = "Saved!";
                    flash_timer = 15;
                    break;
                case A800_MENU_OVERWRITE:
                    SaveState();
                    flash_msg = "Saved!";
                    flash_timer = 15;
                    break;
                case A800_MENU_DELETE:
                    if (a800_delete_save()) {
                        has_save = false;
                        flash_msg = "Deleted!";
                        flash_timer = 15;
                        count = 0;
                        labels[count] = "Resume Game";      ids[count] = A800_MENU_RESUME;    count++;
                        labels[count] = "Restart Game";     ids[count] = A800_MENU_RESTART;   count++;
                        labels[count] = "Save Game";        ids[count] = A800_MENU_SAVE;      count++;
                        labels[count] = "Exit Game";        ids[count] = A800_MENU_EXIT;      count++;
                        if (selected >= count) selected = count - 1;
                    } else {
                        flash_msg = "Error!";
                        flash_timer = 15;
                    }
                    break;
                case A800_MENU_EXIT:
                    menu_active = false;
                    keep_running = false;
                    break;
            }
        }
        prev = state;
    }
    return keep_running;
}

/* ─── Main entry point ────────────────────────────────────────── */
extern "C" void atari800_run(const char *rom_path)
{
    ESP_LOGI(TAG, "atari800_run: starting, ROM=%s", rom_path);
    a800_quit_flag = false;

    odroid_settings_RomFilePath_set(rom_path);

    /* Double-buffer for video */
    lcdfb[0] = (uint8_t *)heap_caps_malloc(DISPLAY_WIDTH * DISPLAY_HEIGHT, MALLOC_CAP_SPIRAM);
    lcdfb[1] = (uint8_t *)heap_caps_malloc(DISPLAY_WIDTH * DISPLAY_HEIGHT, MALLOC_CAP_SPIRAM);
    if (!lcdfb[0] || !lcdfb[1]) {
        ESP_LOGE(TAG, "lcdfb alloc failed");
        goto cleanup;
    }
    lcdfb_write_idx = 0;

    /* Init emulator */
    emu_init(rom_path);

    /* Audio */
    odroid_audio_init(AUDIO_SAMPLE_RATE);

    /* ─── Paddle pot detection on GPIO 52 (ADC2_CH3) ─────────── */
    {
        odroid_paddle_adc_init();
        /* Take 4 samples to check if a real pot is connected (vs floating pin) */
        int lo = 4095, hi = 0;
        for (int i = 0; i < 4; i++) {
            odroid_gamepad_state dummy;
            odroid_input_gamepad_read(&dummy);  /* triggers ADC read */
            int v = odroid_paddle_adc_raw;
            if (v < lo) lo = v;
            if (v > hi) hi = v;
            vTaskDelay(pdMS_TO_TICKS(5));
        }
        int spread = hi - lo;
        if (spread < PADDLE_DETECT_SPREAD) {
            paddle_adc_enabled = true;
            ESP_LOGI(TAG, "Paddle pot DETECTED (ADC spread=%d, lo=%d hi=%d)", spread, lo, hi);
        } else {
            paddle_adc_enabled = false;
            ESP_LOGI(TAG, "Paddle pot NOT detected (ADC spread=%d — floating?), using D-pad fallback", spread);
        }
    }

    /* Save path */
    build_save_path(rom_path);

    /* Auto-load saved state if requested */
    if (odroid_settings_StartAction_get() == ODROID_START_ACTION_RESTART) {
        odroid_settings_StartAction_set(ODROID_START_ACTION_NORMAL);
        if (LoadState())
            ESP_LOGI(TAG, "Resumed from save state");
    }

    /* Video queue and task */
    vidQueue = xQueueCreate(1, sizeof(uint8_t *));
    xTaskCreatePinnedToCore(&videoTask, "a800_video", 1024 * 4, NULL, 5, &videoTaskHandle, 1);

    /* Audio queue and task (async I2S submission on core 1) */
    audioQueue = xQueueCreate(2, sizeof(audio_msg_t));
    xTaskCreatePinnedToCore(&audioTask, "a800_audio", 1024 * 4, NULL, 4, &audioTaskHandle, 1);

    {
        odroid_gamepad_state previousState;
        odroid_input_gamepad_read(&previousState);

        int64_t totalElapsedTime = 0;
        int frame = 0;
        bool ignoreMenuButton = previousState.values[ODROID_INPUT_MENU];

        /* ── Main emulation loop ── */
        while (!a800_quit_flag) {
            odroid_gamepad_state joystick;
            odroid_input_gamepad_read(&joystick);

            if (ignoreMenuButton)
                ignoreMenuButton = previousState.values[ODROID_INPUT_MENU];

            /* MENU button → in-game menu */
            if (!ignoreMenuButton && previousState.values[ODROID_INPUT_MENU] &&
                !joystick.values[ODROID_INPUT_MENU]) {
                uint8_t *dummy;
                while (xQueueReceive(vidQueue, &dummy, 0) == pdTRUE) {}
                vTaskDelay(pdMS_TO_TICKS(50));

                if (!a800_show_ingame_menu()) {
                    /* Exit selected */
                    SaveState();
                    a800_quit_flag = true;
                    break;
                }
                odroid_input_gamepad_read(&joystick);
            }

            /* VOLUME button */
            if (previousState.values[ODROID_INPUT_VOLUME] &&
                !joystick.values[ODROID_INPUT_VOLUME]) {
                a800_show_volume_overlay();
                odroid_input_gamepad_read(&joystick);
            }

            int64_t startTime = esp_timer_get_time();

            emu_step(&joystick);

            previousState = joystick;

            int64_t elapsedTime = esp_timer_get_time() - startTime;
            totalElapsedTime += elapsedTime;
            ++frame;

            if (frame == 60) {
                float seconds = totalElapsedTime / 1000000.0f;
                float fps = frame / seconds;
                ESP_LOGI(TAG, "HEAP:0x%x FPS:%.1f", (unsigned)esp_get_free_heap_size(), fps);
                frame = 0;
                totalElapsedTime = 0;
            }
        }
    }

cleanup:
    /* Stop audio task */
    if (audioQueue && audioTaskIsRunning) {
        audio_msg_t poison = { NULL, 0 };
        xQueueSend(audioQueue, &poison, portMAX_DELAY);
        while (audioTaskIsRunning) vTaskDelay(1);
    }

    /* Stop video task */
    if (vidQueue && videoTaskIsRunning) {
        uint8_t *stop = (uint8_t *)(uintptr_t)1;
        xQueueSend(vidQueue, &stop, portMAX_DELAY);
        while (videoTaskIsRunning) vTaskDelay(1);
    }

    odroid_audio_terminate();

    for (int b = 0; b < 2; b++) {
        if (sampleBuffer[b]) { heap_caps_free(sampleBuffer[b]); sampleBuffer[b] = NULL; }
    }
    snd_buf_idx = 0;
    for (int i = 0; i < 2; i++) {
        if (framebuffer[i]) { heap_caps_free(framebuffer[i]); framebuffer[i] = NULL; }
    }
    if (lcdfb[0]) { heap_caps_free(lcdfb[0]); lcdfb[0] = NULL; }
    if (lcdfb[1]) { heap_caps_free(lcdfb[1]); lcdfb[1] = NULL; }
    if (vidQueue)   { vQueueDelete(vidQueue);   vidQueue   = NULL; }
    if (audioQueue) { vQueueDelete(audioQueue); audioQueue = NULL; }

    a800_quit_flag = false;
    ESP_LOGI(TAG, "atari800_run: returning to launcher");
}
