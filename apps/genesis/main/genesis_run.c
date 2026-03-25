/*
 * genesis_run.c — Entry point for Sega Genesis (Gwenesis) emulator on ESP32-P4
 *
 * Loads a .md/.gen/.bin ROM from SD card into PSRAM, runs the Gwenesis
 * emulator, handles in-game menu/volume, and returns to launcher on exit.
 */

#include "genesis_run.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "esp_heap_caps.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_log.h"

/* Gwenesis core headers */
#include "m68k.h"
#include "z80inst.h"
#include "ym2612.h"
#include "gwenesis_bus.h"
#include "gwenesis_io.h"
#include "gwenesis_vdp.h"
#include "gwenesis_savestate.h"
#include "gwenesis_sn76489.h"

/* Odroid platform */
#include "odroid_settings.h"
#include "odroid_audio.h"
#include "odroid_input.h"
#include "odroid_system.h"
#include "odroid_display.h"
#include "odroid_sdcard.h"
#include "st7701_lcd.h"

static const char *TAG = "GENESIS_RUN";

/* ─── Configuration ─────────────────────────────────────────────── */
#define GEN_FB_W          320
#define GEN_FB_H          224
#define GEN_FB_H_PAL      240

#define AUDIO_SAMPLE_RATE 53267    /* GWENESIS_AUDIO_FREQ_NTSC — full native rate */
#define AUDIO_FRAG_SIZE   512

#ifndef GWENESIS_AUDIO_ACCURATE
#define GWENESIS_AUDIO_ACCURATE 0
#endif

static void genesis_blit_sidebar_buttons(void);
#define TARGET_FPS        60

/* Audio buffer length — max of NTSC/PAL */
#define AUDIO_BUF_LEN     1056     /* GWENESIS_AUDIO_BUFFER_LENGTH_PAL */

/* ─── Module state ─────────────────────────────────────────────── */
static volatile bool genesis_quit_flag = false;
static volatile bool videoTaskRunning  = false;
static volatile bool audioTaskRunning  = false;

/* Gwenesis externs */
extern int zclk;
extern unsigned char *gwenesis_vdp_regs;
extern unsigned short gwenesis_vdp_status;
extern unsigned short *CRAM565;
extern int screen_width, screen_height;
extern int hint_pending;

/* ROM data pointer — defined in gwenesis_bus.c, set by load_cartridge() */
extern unsigned char *ROM_DATA;

/* M68K RAM — declared extern in m68k.h, we provide the definition */
unsigned char *M68K_RAM = NULL;

/* Audio buffers — gwenesis externs */
int16_t *gwenesis_sn76489_buffer = NULL;
int sn76489_index;
int sn76489_clock;
int16_t *gwenesis_ym2612_buffer = NULL;
int ym2612_index;
int ym2612_clock;

/* LFO PM table — declared extern in ym2612.h, we provide the definition */
int32_t *lfo_pm_table = NULL;

/* tl_tab — declared extern in ym2612.h, we provide the definition */
signed int *tl_tab = NULL;

/* sin_tab, OPNREGS — defined in ym2612.c, just use extern */
extern unsigned int *sin_tab;
extern uint8_t *OPNREGS;

/* Z80 RAM — declared extern in gwenesis_bus.h, we provide the definition */
unsigned char *ZRAM = NULL;

/* VDP externs */
extern unsigned char *VRAM;
extern unsigned short *CRAM;
extern unsigned char *SAT_CACHE;
extern unsigned short *fifo;
extern unsigned short *VSRAM;
extern uint8_t *render_buffer;
extern uint8_t *sprite_buffer;

/* M68K CPU state — gwenesis extern */
extern m68ki_cpu_core *m68k;

/* YM2612 state — ym2612 pointer defined in ym2612.c */

/* Framebuffer state */
static uint8_t  *gen_fb[2]  = { NULL, NULL };  /* double-buffer 8-bit indexed */
static uint16_t *gen_rgb565 = NULL;             /* palette-converted RGB565 */
static uint16_t  gen_palette[256];              /* shadow palette (RGB565) */
static int gen_fb_write = 0;

/* Video task */
static QueueHandle_t vidQueue;
static TaskHandle_t  videoTaskHandle;

/* Audio task */
static TaskHandle_t  audioTaskHandle;
#define AUD_QUEUE_DEPTH 2
static int16_t *audio_dma_buf[AUD_QUEUE_DEPTH] = { NULL, NULL };

/* Frame skip — start conservative, can be tuned */
static int frameskip  = 2;    /* render 1 of every N frames */

/* Scanline loop state — scan_line must be non-static (extern'd by core) */
static int system_clock;
int scan_line;
static int frame_counter = 0;

/* ─── Savestate file I/O ───────────────────────────────────────── */
static FILE *savestate_fp = NULL;

typedef struct {
    char key[28];
    uint32_t length;
} svar_t;

SaveState* saveGwenesisStateOpenForRead(const char* fileName)
{
    (void)fileName;
    return (SaveState*)1;
}

SaveState* saveGwenesisStateOpenForWrite(const char* fileName)
{
    (void)fileName;
    return (SaveState*)1;
}

int saveGwenesisStateGet(SaveState* state, const char* tagName)
{
    int value = 0;
    saveGwenesisStateGetBuffer(state, tagName, &value, sizeof(int));
    return value;
}

void saveGwenesisStateSet(SaveState* state, const char* tagName, int value)
{
    saveGwenesisStateSetBuffer(state, tagName, &value, sizeof(int));
}

void saveGwenesisStateGetBuffer(SaveState* state, const char* tagName, void* buffer, int length)
{
    (void)state;
    if (!savestate_fp) return;

    size_t initial_pos = ftell(savestate_fp);
    bool from_start = false;
    svar_t var;

    while (!from_start || (size_t)ftell(savestate_fp) < initial_pos) {
        if (!fread(&var, sizeof(svar_t), 1, savestate_fp)) {
            if (!from_start) {
                fseek(savestate_fp, 0, SEEK_SET);
                from_start = true;
                continue;
            }
            break;
        }
        if (strncmp(var.key, tagName, sizeof(var.key)) == 0) {
            int to_read = (int)var.length < length ? (int)var.length : length;
            fread(buffer, to_read, 1, savestate_fp);
            return;
        }
        fseek(savestate_fp, var.length, SEEK_CUR);
    }
    ESP_LOGW(TAG, "Savestate key '%s' not found", tagName);
}

void saveGwenesisStateSetBuffer(SaveState* state, const char* tagName, const void* buffer, int length)
{
    (void)state;
    if (!savestate_fp) return;

    svar_t var = {{0}, (uint32_t)length};
    strncpy(var.key, tagName, sizeof(var.key) - 1);
    fwrite(&var, sizeof(var), 1, savestate_fp);
    fwrite(buffer, length, 1, savestate_fp);
}

/* ─── Input callback (gwenesis extern) ──────────────────────────── */
void gwenesis_io_get_buttons(void)
{
    /* Input is handled in the main emulation loop instead */
}

/* ─── Minimal 5×5 bitmap font (A-Z) for menu/volume overlay ──── */
static const uint8_t font5x5[][5] = {
    /* A */ {0x0E,0x11,0x1F,0x11,0x11},
    /* B */ {0x1E,0x11,0x1E,0x11,0x1E},
    /* C */ {0x0F,0x10,0x10,0x10,0x0F},
    /* D */ {0x1E,0x11,0x11,0x11,0x1E},
    /* E */ {0x1F,0x10,0x1E,0x10,0x1F},
    /* F */ {0x1F,0x10,0x1E,0x10,0x10},
    /* G */ {0x0F,0x10,0x13,0x11,0x0F},
    /* H */ {0x11,0x11,0x1F,0x11,0x11},
    /* I */ {0x0E,0x04,0x04,0x04,0x0E},
    /* J */ {0x01,0x01,0x01,0x11,0x0E},
    /* K */ {0x11,0x12,0x1C,0x12,0x11},
    /* L */ {0x10,0x10,0x10,0x10,0x1F},
    /* M */ {0x11,0x1B,0x15,0x11,0x11},
    /* N */ {0x11,0x19,0x15,0x13,0x11},
    /* O */ {0x0E,0x11,0x11,0x11,0x0E},
    /* P */ {0x1E,0x11,0x1E,0x10,0x10},
    /* Q */ {0x0E,0x11,0x15,0x12,0x0D},
    /* R */ {0x1E,0x11,0x1E,0x12,0x11},
    /* S */ {0x0F,0x10,0x0E,0x01,0x1E},
    /* T */ {0x1F,0x04,0x04,0x04,0x04},
    /* U */ {0x11,0x11,0x11,0x11,0x0E},
    /* V */ {0x11,0x11,0x11,0x0A,0x04},
    /* W */ {0x11,0x11,0x15,0x1B,0x11},
    /* X */ {0x11,0x0A,0x04,0x0A,0x11},
    /* Y */ {0x11,0x0A,0x04,0x04,0x04},
    /* Z */ {0x1F,0x02,0x04,0x08,0x1F},
};

#define DCOL_BLACK  0x0000
#define DCOL_WHITE  0xFFFF
#define DCOL_GREEN  0x07E0
#define DCOL_RED    0xF800
#define DCOL_YELLOW 0xFFE0

static void menu_draw_char(uint16_t *fb, int stride, int cx, int cy, int h, char ch, uint16_t color)
{
    int idx = -1;
    if (ch >= 'A' && ch <= 'Z') idx = ch - 'A';
    else if (ch >= 'a' && ch <= 'z') idx = ch - 'a';
    if (idx < 0) return;
    for (int row = 0; row < 5; row++)
        for (int col = 0; col < 5; col++)
            if (font5x5[idx][row] & (0x10 >> col))
                if ((cy + row) < h && (cx + col) < stride)
                    fb[(cy + row) * stride + (cx + col)] = color;
}

static void menu_draw_str(uint16_t *fb, int stride, int x, int y, int h, const char *s, uint16_t color)
{
    while (*s) { menu_draw_char(fb, stride, x, y, h, *s++, color); x += 6; }
}

/* ─── Palette conversion: 8-bit indexed → RGB565 ────────────────── */
/* src stride is always 320 (VDP framebuffer), but visible width may be 256 (H32) */
static void gen_convert_frame(const uint8_t *src, uint16_t *dst,
                              const uint16_t *palette, int w, int h)
{
    const int stride = 320;  /* VDP framebuffer stride is always 320 */
    for (int y = 0; y < h; y++) {
        const uint8_t *row = src + y * stride;
        uint16_t *out = dst + y * w;
        int x = 0;
        /* Process 4 pixels at a time for PSRAM cache efficiency */
        for (; x + 3 < w; x += 4) {
            uint32_t quad = *(const uint32_t *)(row + x);
            out[x + 0] = palette[(quad >>  0) & 0xFF];
            out[x + 1] = palette[(quad >>  8) & 0xFF];
            out[x + 2] = palette[(quad >> 16) & 0xFF];
            out[x + 3] = palette[(quad >> 24) & 0xFF];
        }
        for (; x < w; x++) {
            out[x] = palette[row[x]];
        }
    }
}

/* ─── Video Task (Core 1) ──────────────────────────────────────── */
static void genesis_video_task(void *arg)
{
    uint8_t *frame = NULL;
    videoTaskRunning = true;
    int sidebar_countdown = 2;

    ESP_LOGI(TAG, "Video task started on core %d", xPortGetCoreID());

    while (1) {
        xQueuePeek(vidQueue, &frame, portMAX_DELAY);

        if (frame == (uint8_t *)1) break;  /* Quit sentinel */

        /* Palette convert 8-bit indexed → RGB565 */
        int w = (screen_width == 256) ? 256 : 320;
        int h = (int)screen_height;

        gen_convert_frame(frame, gen_rgb565, gen_palette, w, h);

        /* If H32 mode (256 wide), use the custom PPA path directly at 256×224 */
        if (w == 256) {
            ili9341_write_frame_rgb565_custom(gen_rgb565, 256, h, 2.0f, false);
        } else {
            /* H40 mode: 320×224 — scale 2× + rotate via PPA */
            ili9341_write_frame_rgb565_custom(gen_rgb565, 320, h, 2.0f, false);
        }

        /* Draw sidebar buttons on first frames */
        if (sidebar_countdown > 0) {
            genesis_blit_sidebar_buttons();
            sidebar_countdown--;
        }

        xQueueReceive(vidQueue, &frame, portMAX_DELAY);
    }

    videoTaskRunning = false;
    vTaskDelete(NULL);
}

/* ─── Audio Task (Core 1) ──────────────────────────────────────── */
static void genesis_audio_task(void *arg)
{
    audioTaskRunning = true;
    int dma_idx = 0;

    while (1) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        if (genesis_quit_flag) break;

        /* Mix SN76489 + YM2612 into a single stereo-interleaved buffer */
        int audio_len = sn76489_index > ym2612_index ? sn76489_index : ym2612_index;
        if (audio_len <= 0) continue;

        int16_t *dest = audio_dma_buf[dma_idx];
        int remaining = audio_len;
        int src_off = 0;

        while (remaining > 0) {
            int n = (remaining < AUDIO_FRAG_SIZE) ? remaining : AUDIO_FRAG_SIZE;

            /* Mix mono into interleaved stereo with clamping */
            for (int i = 0; i < n; i++) {
                int32_t sn = (src_off + i < sn76489_index) ? gwenesis_sn76489_buffer[src_off + i] : 0;
                int32_t ym = (src_off + i < ym2612_index)  ? gwenesis_ym2612_buffer[src_off + i]  : 0;
                int32_t sum = sn + ym;
                if (sum > 32767) sum = 32767;
                if (sum < -32768) sum = -32768;
                dest[i * 2 + 0] = (int16_t)sum;  /* L */
                dest[i * 2 + 1] = (int16_t)sum;  /* R */
            }

            odroid_audio_submit(dest, n);
            src_off += n;
            remaining -= n;
        }

        dma_idx = (dma_idx + 1) % AUD_QUEUE_DEPTH;
    }

    audioTaskRunning = false;
    vTaskDelete(NULL);
}

/* ─── Save state helpers ───────────────────────────────────────── */

static char *genesis_get_save_path(void)
{
    char *romName = odroid_settings_RomFilePath_get();
    if (!romName) return NULL;
    char *fileName = odroid_util_GetFileNameWithoutExtension(romName);
    free(romName);
    if (!fileName) return NULL;
    char pathBuf[256];
    snprintf(pathBuf, sizeof(pathBuf), "/sd/odroid/data/genesis/%s.sav", fileName);
    free(fileName);
    return strdup(pathBuf);
}

static bool genesis_check_save_exists(void)
{
    char *path = genesis_get_save_path();
    if (!path) return false;
    struct stat st;
    bool exists = (stat(path, &st) == 0);
    free(path);
    return exists;
}

static bool genesis_do_save_state(void)
{
    char *path = genesis_get_save_path();
    if (!path) return false;

    esp_err_t r = odroid_sdcard_open("/sd");
    if (r != ESP_OK) { free(path); return false; }

    mkdir("/sd/odroid", 0775);
    mkdir("/sd/odroid/data", 0775);
    mkdir("/sd/odroid/data/genesis", 0775);

    savestate_fp = fopen(path, "wb");
    bool ok = false;
    if (savestate_fp) {
        gwenesis_save_state();
        fclose(savestate_fp);
        savestate_fp = NULL;
        ok = true;
    }
    free(path);
    odroid_sdcard_close();
    return ok;
}

static bool genesis_do_load_state(void)
{
    char *path = genesis_get_save_path();
    if (!path) return false;

    esp_err_t r = odroid_sdcard_open("/sd");
    if (r != ESP_OK) { free(path); return false; }

    savestate_fp = fopen(path, "rb");
    bool ok = false;
    if (savestate_fp) {
        gwenesis_load_state();
        fclose(savestate_fp);
        savestate_fp = NULL;
        ok = true;
    }
    free(path);
    odroid_sdcard_close();
    return ok;
}

/* ─── Volume overlay (same pattern as SNES) ────────────────────── */
static void genesis_show_volume(void)
{
    static const char * const level_names[] = { "MUTE", "25%", "50%", "75%", "100%" };

    int level = (int)odroid_audio_volume_get();
    level = (level + 1) % ODROID_VOLUME_LEVEL_COUNT;
    odroid_audio_volume_set(level);
    odroid_settings_Volume_set(level);

    int timeout = 25;
    uint16_t *fb = gen_rgb565;
    if (!fb) return;

    odroid_gamepad_state prev;
    odroid_input_gamepad_read(&prev);

    /* Debounce */
    for (int i = 0; i < 100; i++) {
        odroid_gamepad_state tmp;
        odroid_input_gamepad_read(&tmp);
        if (!tmp.values[ODROID_INPUT_VOLUME]) break;
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    odroid_input_gamepad_read(&prev);

    int fw = (screen_width == 256) ? 256 : 320;
    int fh = (int)screen_height;

    while (timeout > 0) {
        /* Redraw current frame as background */
        gen_convert_frame(gen_fb[gen_fb_write], fb, gen_palette, fw, fh);

        int box_w = 140, box_h = 34;
        int box_x = (fw - box_w) / 2;
        int box_y = 4;

        /* Dark background + border */
        for (int r = box_y; r < box_y + box_h && r < fh; r++)
            for (int c = box_x; c < box_x + box_w && c < fw; c++)
                fb[r * fw + c] = DCOL_BLACK;
        for (int c = box_x; c < box_x + box_w; c++) {
            if (box_y < fh) fb[box_y * fw + c] = DCOL_WHITE;
            if (box_y + box_h - 1 < fh) fb[(box_y + box_h - 1) * fw + c] = DCOL_WHITE;
        }
        for (int r = box_y; r < box_y + box_h && r < fh; r++) {
            fb[r * fw + box_x] = DCOL_WHITE;
            fb[r * fw + box_x + box_w - 1] = DCOL_WHITE;
        }

        char title[32];
        snprintf(title, sizeof(title), "VOL %s", level_names[level]);
        menu_draw_str(fb, fw, box_x + 8, box_y + 4, fh, title, DCOL_YELLOW);

        /* Volume bar */
        int bar_x = box_x + 6, bar_y = box_y + 16;
        int bar_w = box_w - 12, bar_h = 10;
        for (int r = bar_y; r < bar_y + bar_h && r < fh; r++)
            for (int c = bar_x; c < bar_x + bar_w && c < fw; c++)
                fb[r * fw + c] = 0x2104;
        if (level > 0) {
            int fill = (bar_w * level) / (ODROID_VOLUME_LEVEL_COUNT - 1);
            uint16_t bar_col = (level <= 1) ? 0x07E0 : (level <= 3) ? 0x07FF : DCOL_YELLOW;
            for (int r = bar_y; r < bar_y + bar_h && r < fh; r++)
                for (int c = bar_x; c < bar_x + fill && c < fw; c++)
                    fb[r * fw + c] = bar_col;
        }

        ili9341_write_frame_rgb565_custom(fb, fw, fh, 2.0f, false);

        vTaskDelay(pdMS_TO_TICKS(80));
        odroid_gamepad_state state;
        odroid_input_gamepad_read(&state);

        if (state.values[ODROID_INPUT_LEFT] && !prev.values[ODROID_INPUT_LEFT]) {
            if (level > 0) level--;
            odroid_audio_volume_set(level);
            odroid_settings_Volume_set(level);
            timeout = 25;
        }
        if (state.values[ODROID_INPUT_RIGHT] && !prev.values[ODROID_INPUT_RIGHT]) {
            if (level < ODROID_VOLUME_LEVEL_COUNT - 1) level++;
            odroid_audio_volume_set(level);
            odroid_settings_Volume_set(level);
            timeout = 25;
        }
        if (state.values[ODROID_INPUT_VOLUME] && !prev.values[ODROID_INPUT_VOLUME]) {
            level = (level + 1) % ODROID_VOLUME_LEVEL_COUNT;
            odroid_audio_volume_set(level);
            odroid_settings_Volume_set(level);
            timeout = 25;
        }
        if ((state.values[ODROID_INPUT_A] && !prev.values[ODROID_INPUT_A]) ||
            (state.values[ODROID_INPUT_B] && !prev.values[ODROID_INPUT_B])) {
            break;
        }

        prev = state;
        timeout--;
    }
}

/* ─── In-game menu (same pattern as SNES) ──────────────────────── */
static bool genesis_show_menu(void)
{
    uint16_t *fb = gen_rgb565;
    if (!fb) return false;

    int fw = (screen_width == 256) ? 256 : 320;
    int fh = (int)screen_height;

    int sel = 0;
    bool has_save = genesis_check_save_exists();
    const int ITEMS = 4;
    const char *labels[] = { "RESUME", "SAVE STATE", "LOAD STATE", "EXIT GAME" };

    odroid_gamepad_state prev;
    odroid_input_gamepad_read(&prev);

    /* Debounce */
    for (int i = 0; i < 50; i++) {
        odroid_gamepad_state tmp;
        odroid_input_gamepad_read(&tmp);
        if (!tmp.values[ODROID_INPUT_MENU]) break;
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    const char *status_msg = NULL;
    uint16_t status_color = DCOL_GREEN;
    int status_timeout = 0;

    while (1) {
        /* Redraw current frame as background */
        gen_convert_frame(gen_fb[gen_fb_write], fb, gen_palette, fw, fh);

        int box_w = 140, box_h = 18 + ITEMS * 14;
        int box_x = (fw - box_w) / 2;
        int box_y = (fh - box_h) / 2;

        /* Background */
        for (int r = box_y; r < box_y + box_h && r < fh; r++)
            for (int c = box_x; c < box_x + box_w && c < fw; c++)
                fb[r * fw + c] = DCOL_BLACK;

        /* Border */
        for (int c = box_x; c < box_x + box_w; c++) {
            fb[box_y * fw + c] = DCOL_WHITE;
            fb[(box_y + box_h - 1) * fw + c] = DCOL_WHITE;
        }
        for (int r = box_y; r < box_y + box_h; r++) {
            fb[r * fw + box_x] = DCOL_WHITE;
            fb[r * fw + box_x + box_w - 1] = DCOL_WHITE;
        }

        /* Menu items */
        for (int i = 0; i < ITEMS; i++) {
            bool greyed = (i == 2 && !has_save);
            uint16_t color = greyed ? 0x4208 : (i == sel) ? DCOL_GREEN : DCOL_WHITE;
            int ty = box_y + 10 + i * 14;
            int tx = box_x + 20;

            if (i == sel) {
                for (int dy = 0; dy < 5; dy++)
                    for (int dx = 0; dx < 3; dx++)
                        fb[(ty + dy) * fw + tx - 10 + dx] = color;
            }

            menu_draw_str(fb, fw, tx, ty, fh, labels[i], color);
        }

        if (status_msg && status_timeout > 0) {
            menu_draw_str(fb, fw, box_x + 8, box_y + box_h + 4, fh, status_msg, status_color);
            status_timeout--;
        }

        ili9341_write_frame_rgb565_custom(fb, fw, fh, 2.0f, false);

        vTaskDelay(pdMS_TO_TICKS(100));

        odroid_gamepad_state gp;
        odroid_input_gamepad_read(&gp);

        if (gp.values[ODROID_INPUT_UP] && !prev.values[ODROID_INPUT_UP])
            sel = (sel - 1 + ITEMS) % ITEMS;
        if (gp.values[ODROID_INPUT_DOWN] && !prev.values[ODROID_INPUT_DOWN])
            sel = (sel + 1) % ITEMS;
        if (gp.values[ODROID_INPUT_A] && !prev.values[ODROID_INPUT_A]) {
            if (sel == 0) return false;  /* Resume */
            if (sel == 1) {              /* Save State */
                if (genesis_do_save_state()) {
                    status_msg = "SAVED OK";
                    status_color = DCOL_GREEN;
                    has_save = true;
                } else {
                    status_msg = "SAVE FAILED";
                    status_color = DCOL_RED;
                }
                status_timeout = 15;
            }
            if (sel == 2 && has_save) {  /* Load State */
                if (genesis_do_load_state()) {
                    return false;
                } else {
                    status_msg = "LOAD FAILED";
                    status_color = DCOL_RED;
                    status_timeout = 15;
                }
            }
            if (sel == 3) return true;   /* Exit */
        }
        if (gp.values[ODROID_INPUT_MENU] && !prev.values[ODROID_INPUT_MENU])
            return false;
        if (gp.values[ODROID_INPUT_B] && !prev.values[ODROID_INPUT_B])
            return false;

        prev = gp;
    }
}

/* ─── Sidebar button labels ───────────────────────────────────── */
static uint16_t *s_sidebar_buf[2] = { NULL, NULL };
static const struct { const char *text; int px, py, pw, ph; } s_sidebar_btns[] = {
    { "MENU", 200, 2,   80, 76 },   /* left sidebar: portrait y 0-79 (80px for 640-wide output) */
    { "VOL",  200, 722, 80, 76 },   /* right sidebar: portrait y 720-799 */
};

static void genesis_init_sidebar_buttons(void)
{
    enum { SC = 3 };
    enum { CW = 5 * SC, CH = 5 * SC, GAP = SC };
    const uint16_t COL_BG  = 0x18E3;
    const uint16_t COL_BRD = 0x6B4D;
    const uint16_t COL_TXT = 0xFFFF;

    for (int b = 0; b < 2; b++) {
        const int pw = s_sidebar_btns[b].pw, ph = s_sidebar_btns[b].ph;

        s_sidebar_buf[b] = (uint16_t *)heap_caps_aligned_calloc(
            64, pw * ph, sizeof(uint16_t),
            MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA);
        if (!s_sidebar_buf[b]) { ESP_LOGE(TAG, "Sidebar buf alloc failed b=%d", b); continue; }

        uint16_t *buf = s_sidebar_buf[b];

        for (int i = 0; i < pw * ph; i++) buf[i] = COL_BG;

        /* 2-pixel border */
        for (int t = 0; t < 2; t++) {
            for (int x = 0; x < pw; x++) {
                buf[t * pw + x] = COL_BRD;
                buf[(ph - 1 - t) * pw + x] = COL_BRD;
            }
            for (int y = 0; y < ph; y++) {
                buf[y * pw + t] = COL_BRD;
                buf[y * pw + pw - 1 - t] = COL_BRD;
            }
        }

        /* Render upright text */
        const char *s = s_sidebar_btns[b].text;
        int nch = strlen(s);
        int txt_pw = CH;
        int txt_ph = nch * (CW + GAP) - GAP;
        int ox = (pw - txt_pw) / 2;
        int oy = (ph - txt_ph) / 2;
        int glyph_top_x = ox + txt_pw - 1;

        for (int ci = 0; ci < nch; ci++) {
            int idx = -1;
            char ch = s[ci];
            if (ch >= 'A' && ch <= 'Z') idx = ch - 'A';
            else if (ch >= 'a' && ch <= 'z') idx = ch - 'a';
            if (idx < 0) continue;

            int char_by = oy + ci * (CW + GAP);

            for (int fr = 0; fr < 5; fr++)
                for (int fc = 0; fc < 5; fc++)
                    if (font5x5[idx][fr] & (0x10 >> fc))
                        for (int sr = 0; sr < SC; sr++)
                            for (int sc = 0; sc < SC; sc++) {
                                int bx = glyph_top_x - (fr * SC + sr);
                                int by = char_by + fc * SC + sc;
                                if (bx >= 0 && bx < pw && by >= 0 && by < ph)
                                    buf[by * pw + bx] = COL_TXT;
                            }
        }
    }
}

static void genesis_blit_sidebar_buttons(void)
{
    for (int b = 0; b < 2; b++) {
        if (!s_sidebar_buf[b]) continue;
        st7701_lcd_draw_to_fb(
            (uint16_t)s_sidebar_btns[b].px, (uint16_t)s_sidebar_btns[b].py,
            (uint16_t)s_sidebar_btns[b].pw, (uint16_t)s_sidebar_btns[b].ph,
            s_sidebar_buf[b]);
    }
}

/* ─── Memory allocation (replaces shared_memory / box-emu) ──────── */
static void genesis_alloc_core(void)
{
    /* M68K CPU state — hottest structure, accessed every opcode → internal RAM */
    m68k = (m68ki_cpu_core *)heap_caps_calloc(1, sizeof(m68ki_cpu_core),
                                               MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);

    /* M68K RAM — 64 KB — every RAM read/write → internal RAM */
    M68K_RAM = (uint8_t *)heap_caps_calloc(1, MAX_RAM_SIZE,
                                            MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);

    /* Z80 RAM — 8 KB → internal RAM */
    ZRAM = (uint8_t *)heap_caps_calloc(1, MAX_Z80_RAM_SIZE,
                                        MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);

    /* VRAM — 64 KB → internal RAM (VDP rendering hot path) */
    VRAM = (uint8_t *)heap_caps_calloc(1, VRAM_MAX_SIZE,
                                        MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!VRAM) {
        ESP_LOGW(TAG, "VRAM internal alloc failed, falling back to PSRAM");
        VRAM = (uint8_t *)heap_caps_calloc(1, VRAM_MAX_SIZE,
                                            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    }

    /* CRAM, VSRAM, SAT cache, VDP regs, FIFO — small, hot → internal RAM */
    CRAM      = (uint16_t *)heap_caps_calloc(1, CRAM_MAX_SIZE * sizeof(uint16_t),
                                              MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    CRAM565   = (uint16_t *)heap_caps_calloc(1, CRAM_MAX_SIZE * 4 * sizeof(uint16_t),
                                              MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    VSRAM     = (uint16_t *)heap_caps_calloc(1, VSRAM_MAX_SIZE * sizeof(uint16_t),
                                              MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    SAT_CACHE = (uint8_t *)heap_caps_calloc(1, SAT_CACHE_MAX_SIZE,
                                             MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    gwenesis_vdp_regs = (uint8_t *)heap_caps_calloc(1, REG_SIZE,
                                                      MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    fifo = (uint16_t *)heap_caps_calloc(1, FIFO_SIZE * sizeof(uint16_t),
                                         MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);

    /* Render / sprite line buffers — per-pixel writes during rendering → internal RAM */
    render_buffer = (uint8_t *)heap_caps_calloc(1, SCREEN_WIDTH + PIX_OVERFLOW * 2,
                                                 MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    sprite_buffer = (uint8_t *)heap_caps_calloc(1, SCREEN_WIDTH + PIX_OVERFLOW * 2,
                                                 MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);

    /* YM2612 state — hot during per-scanline synthesis → internal RAM */
    ym2612 = (YM2612 *)heap_caps_calloc(1, sizeof(YM2612),
                                         MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!ym2612) {
        ESP_LOGW(TAG, "YM2612 internal alloc failed, falling back to PSRAM");
        ym2612 = (YM2612 *)heap_caps_calloc(1, sizeof(YM2612),
                                             MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    }
    OPNREGS = (uint8_t *)heap_caps_calloc(1, 512, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    sin_tab = (unsigned int *)heap_caps_calloc(1, SIN_LEN * sizeof(unsigned int),
                                                MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    tl_tab = (signed int *)heap_caps_calloc(1, 13 * 2 * 256 * sizeof(signed int),
                                             MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!tl_tab) {
        ESP_LOGW(TAG, "tl_tab internal alloc failed (26KB), falling back to PSRAM");
        tl_tab = (signed int *)heap_caps_calloc(1, 13 * 2 * 256 * sizeof(signed int),
                                                 MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    }

    /* LFO PM table — 128 KB, PSRAM */
    lfo_pm_table = (int32_t *)heap_caps_calloc(1, 128 * 8 * 32 * sizeof(int32_t),
                                                MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);

    /* Audio buffers — internal RAM to avoid PSRAM jitter during mixing */
    gwenesis_sn76489_buffer = (int16_t *)heap_caps_calloc(1, AUDIO_BUF_LEN * sizeof(int16_t),
                                                           MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    gwenesis_ym2612_buffer  = (int16_t *)heap_caps_calloc(1, AUDIO_BUF_LEN * sizeof(int16_t),
                                                           MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);

    ESP_LOGI(TAG, "Core memory allocated: m68k=%p M68K_RAM=%p ZRAM=%p VRAM=%p",
             m68k, M68K_RAM, ZRAM, VRAM);
    ESP_LOGI(TAG, "Free internal RAM: %u KB, free PSRAM: %u KB",
             (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024),
             (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024));
}

static void genesis_free_core(void)
{
    heap_caps_free(m68k);          m68k = NULL;
    heap_caps_free(M68K_RAM);      M68K_RAM = NULL;
    heap_caps_free(ZRAM);          ZRAM = NULL;
    heap_caps_free(VRAM);          VRAM = NULL;
    heap_caps_free(CRAM);          CRAM = NULL;
    heap_caps_free(CRAM565);       CRAM565 = NULL;
    heap_caps_free(VSRAM);         VSRAM = NULL;
    heap_caps_free(SAT_CACHE);     SAT_CACHE = NULL;
    heap_caps_free(gwenesis_vdp_regs); gwenesis_vdp_regs = NULL;
    heap_caps_free(fifo);          fifo = NULL;
    heap_caps_free(render_buffer); render_buffer = NULL;
    heap_caps_free(sprite_buffer); sprite_buffer = NULL;

    heap_caps_free(ym2612);  ym2612 = NULL;
    heap_caps_free(OPNREGS);  OPNREGS = NULL;
    heap_caps_free(sin_tab);  sin_tab = NULL;
    heap_caps_free(tl_tab);   tl_tab = NULL;
    heap_caps_free(lfo_pm_table); lfo_pm_table = NULL;
    heap_caps_free(gwenesis_sn76489_buffer); gwenesis_sn76489_buffer = NULL;
    heap_caps_free(gwenesis_ym2612_buffer);  gwenesis_ym2612_buffer = NULL;
}

/* ─── Main entry point ──────────────────────────────────────────── */
void genesis_run(const char *rom_path)
{
    ESP_LOGI(TAG, "Starting Genesis emulator, ROM=%s", rom_path);

    genesis_quit_flag = false;
    frame_counter = 0;

    /* ── Allocate core memory ── */
    genesis_alloc_core();

    /* ── Allocate framebuffers ── */
    for (int i = 0; i < 2; i++) {
        gen_fb[i] = (uint8_t *)heap_caps_calloc(1, GEN_FB_W * GEN_FB_H_PAL,
                                                  MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!gen_fb[i]) {
            ESP_LOGE(TAG, "Failed to allocate gen_fb[%d]", i);
            goto cleanup;
        }
    }
    gen_fb_write = 0;

    /* RGB565 conversion buffer — 320×240 × 2 bytes */
    gen_rgb565 = (uint16_t *)heap_caps_aligned_calloc(
        64, GEN_FB_W * GEN_FB_H_PAL, sizeof(uint16_t),
        MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA);
    if (!gen_rgb565) {
        ESP_LOGE(TAG, "Failed to allocate gen_rgb565");
        goto cleanup;
    }

    /* Audio DMA double-buffers */
    for (int i = 0; i < AUD_QUEUE_DEPTH; i++) {
        audio_dma_buf[i] = (int16_t *)heap_caps_malloc(AUDIO_FRAG_SIZE * 4,
            MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        if (!audio_dma_buf[i]) {
            ESP_LOGE(TAG, "Failed to allocate audio DMA buffer %d", i);
            goto cleanup;
        }
    }

    /* ── Load ROM from SD card ── */
    {
        esp_err_t r = odroid_sdcard_open("/sd");
        if (r != ESP_OK) {
            ESP_LOGE(TAG, "SD card open failed (%d)", (int)r);
            goto cleanup;
        }

        FILE *f = fopen(rom_path, "rb");
        if (!f) {
            ESP_LOGE(TAG, "Failed to open ROM: %s", rom_path);
            odroid_sdcard_close();
            goto cleanup;
        }

        fseek(f, 0, SEEK_END);
        long rom_size = ftell(f);
        fseek(f, 0, SEEK_SET);

        ESP_LOGI(TAG, "ROM size: %ld bytes", rom_size);

        if (rom_size > MAX_ROM_SIZE) {
            ESP_LOGE(TAG, "ROM too large (%ld > %d)", rom_size, MAX_ROM_SIZE);
            fclose(f);
            odroid_sdcard_close();
            goto cleanup;
        }

        /* Allocate ROM buffer in PSRAM */
        ROM_DATA = (unsigned char *)heap_caps_malloc(rom_size,
            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!ROM_DATA) {
            ESP_LOGE(TAG, "Failed to allocate ROM buffer (%ld bytes)", rom_size);
            fclose(f);
            odroid_sdcard_close();
            goto cleanup;
        }

        size_t bytes_read = fread(ROM_DATA, 1, rom_size, f);
        fclose(f);
        odroid_sdcard_close();

        ESP_LOGI(TAG, "ROM loaded: %d bytes", (int)bytes_read);

        /* Initialize gwenesis core */
        load_cartridge(ROM_DATA, bytes_read);
        power_on();
        reset_emulation();
    }

    /* ── Set initial VDP framebuffer ── */
    gwenesis_vdp_set_buffer(gen_fb[0]);

    /* ── Audio init ── */
    int audio_rate = REG1_PAL ? GWENESIS_AUDIO_FREQ_PAL : GWENESIS_AUDIO_FREQ_NTSC;
    odroid_audio_init(audio_rate);
    ESP_LOGI(TAG, "Audio sample rate: %d Hz", audio_rate);

    /* ── Pre-render sidebar buttons ── */
    genesis_init_sidebar_buttons();

    /* Genesis 3-button pad needs X/Y as face buttons (A/C).
     * MENU and VOLUME are handled by the touchscreen shoulder zones. */
    extern bool odroid_input_xy_menu_disable;
    odroid_input_xy_menu_disable = true;

    /* ── Create video queue and task ── */
    vidQueue = xQueueCreate(1, sizeof(uint8_t *));
    xTaskCreatePinnedToCore(genesis_video_task, "gen_video", 4096,
                            NULL, 5, &videoTaskHandle, 1);

    /* ── Create audio task ── */
    xTaskCreatePinnedToCore(genesis_audio_task, "gen_audio", 4096,
                            NULL, 6, &audioTaskHandle, 1);

    /* ── Emulation loop ── */
    int64_t fps_timer = esp_timer_get_time();
    int64_t frame_timer = esp_timer_get_time();
    const int64_t target_frame_us = REG1_PAL ? 20000 : 16667;
    int frame_no = 0;

    /* Profiling */
    int64_t prof_cpu_acc = 0, prof_aud_acc = 0, prof_vid_acc = 0;
    int prof_cnt = 0, prof_skip = 0;

    odroid_gamepad_state gp_prev;
    odroid_input_gamepad_read(&gp_prev);

    ESP_LOGI(TAG, "Entering emulation loop (%s)", REG1_PAL ? "PAL" : "NTSC");

    while (!genesis_quit_flag) {
        /* ── Input: check menu / volume (edge-triggered) ── */
        odroid_gamepad_state gp;
        odroid_input_gamepad_read(&gp);

        if (gp.values[ODROID_INPUT_VOLUME] && !gp_prev.values[ODROID_INPUT_VOLUME]) {
            genesis_show_volume();
            fps_timer = esp_timer_get_time();
            frame_timer = esp_timer_get_time();
            frame_no = 0;
        }

        if (gp.values[ODROID_INPUT_MENU] && !gp_prev.values[ODROID_INPUT_MENU]) {
            if (genesis_show_menu()) {
                genesis_quit_flag = true;
                break;
            }
            fps_timer = esp_timer_get_time();
            frame_timer = esp_timer_get_time();
            frame_no = 0;
        }

        /* ── Input: map gamepad → Genesis pad buttons ── */
        /* Genesis 3-button: Up, Down, Left, Right, A, B, C, Start */
        /* Button enum from gwenesis_bus.h: UP=0 DOWN=1 LEFT=2 RIGHT=3 B=4 C=5 A=6 S=7 */
        static const int btn_map[] = {
            /* ODROID button index → Genesis PAD button */
            -1  /* will be handled below */
        };

        /* Clear all buttons, then set pressed ones */
        for (int b = 0; b <= PAD_S; b++)
            gwenesis_io_pad_release_button(0, b);

        if (gp.values[ODROID_INPUT_UP])     gwenesis_io_pad_press_button(0, PAD_UP);
        if (gp.values[ODROID_INPUT_DOWN])   gwenesis_io_pad_press_button(0, PAD_DOWN);
        if (gp.values[ODROID_INPUT_LEFT])   gwenesis_io_pad_press_button(0, PAD_LEFT);
        if (gp.values[ODROID_INPUT_RIGHT])  gwenesis_io_pad_press_button(0, PAD_RIGHT);
        if (gp.values[ODROID_INPUT_A])      gwenesis_io_pad_press_button(0, PAD_B);     /* A button → Genesis B */
        if (gp.values[ODROID_INPUT_B])      gwenesis_io_pad_press_button(0, PAD_C);     /* B button → Genesis C */
        if (gp.values[ODROID_INPUT_X])      gwenesis_io_pad_press_button(0, PAD_A);     /* X button → Genesis A */
        if (gp.values[ODROID_INPUT_Y])      gwenesis_io_pad_press_button(0, PAD_A);     /* Y button → Genesis A (alt) */
        if (gp.values[ODROID_INPUT_START])  gwenesis_io_pad_press_button(0, PAD_S);     /* Start → Genesis Start */

        gp_prev = gp;

        /* ── Run one Genesis frame ── */
        int64_t t0 = esp_timer_get_time();

        bool drawFrame = (frame_counter % frameskip) == 0;

        int lines_per_frame = REG1_PAL ? LINES_PER_FRAME_PAL : LINES_PER_FRAME_NTSC;
        int hint_counter = gwenesis_vdp_regs[10];

        screen_width  = REG12_MODE_H40 ? 320 : 256;
        screen_height = REG1_PAL ? 240 : 224;

        gwenesis_vdp_render_config();

        /* Reset clocks */
        system_clock = 0;
        zclk = 0;

        ym2612_clock = 0;
        ym2612_index = 0;
        sn76489_clock = 0;
        sn76489_index = 0;

        scan_line = 0;

        while (scan_line < lines_per_frame) {
            system_clock += VDP_CYCLES_PER_LINE;

            m68k_run(system_clock);
            z80_run(system_clock);

            /* Audio — line-accurate mode */
            if (GWENESIS_AUDIO_ACCURATE == 0) {
                gwenesis_SN76489_run(system_clock);
                ym2612_run(system_clock);
            }

            /* Video */
            if (drawFrame && scan_line < (int)screen_height)
                gwenesis_vdp_render_line(scan_line);

            /* H-interrupt counter */
            if ((scan_line == 0) || (scan_line > (int)screen_height)) {
                hint_counter = REG10_LINE_COUNTER;
            }

            if (--hint_counter < 0) {
                if ((REG0_LINE_INTERRUPT != 0) && (scan_line <= (int)screen_height)) {
                    hint_pending = 1;
                    if ((gwenesis_vdp_status & STATUS_VIRQPENDING) == 0)
                        m68k_update_irq(4);
                }
                hint_counter = REG10_LINE_COUNTER;
            }

            scan_line++;

            /* VBlank */
            if (scan_line == (int)screen_height) {
                if (REG1_VBLANK_INTERRUPT != 0) {
                    gwenesis_vdp_status |= STATUS_VIRQPENDING;
                    m68k_set_irq(6);
                }
                z80_irq_line(1);
            }
            if (scan_line == (int)screen_height + 1) {
                z80_irq_line(0);
            }
        }

        /* Complete audio for cycle-accurate mode */
        if (GWENESIS_AUDIO_ACCURATE == 1) {
            gwenesis_SN76489_run(system_clock);
            ym2612_run(system_clock);
        }

        /* Reset M68K cycle counter for next frame */
        m68k->cycles -= system_clock;

        int64_t t1 = esp_timer_get_time();

        /* ── Push frame to display task ── */
        if (drawFrame) {
            /* Copy palette */
            memcpy(gen_palette, CRAM565, 256 * sizeof(uint16_t));

            /* Queue framebuffer to video task */
            void *arg = gen_fb[gen_fb_write];
            xQueueOverwrite(vidQueue, &arg);

            /* Swap framebuffer */
            gen_fb_write ^= 1;
            gwenesis_vdp_set_buffer(gen_fb[gen_fb_write]);
        }

        int64_t t2 = esp_timer_get_time();

        /* ── Signal audio task ── */
        xTaskNotifyGive(audioTaskHandle);

        int64_t t3 = esp_timer_get_time();

        /* Profiling accumulation */
        prof_cpu_acc += (t1 - t0);
        prof_vid_acc += (t2 - t1);
        prof_aud_acc += (t3 - t2);
        prof_cnt++;
        if (!drawFrame) prof_skip++;

        /* Frame pacing */
        frame_counter++;
        frame_no++;

        int64_t elapsed = t3 - frame_timer;
        frame_timer = t3;

        if (elapsed < target_frame_us) {
            int sleep_us = (int)(target_frame_us - elapsed);
            if (sleep_us > 500)
                vTaskDelay(pdMS_TO_TICKS(sleep_us / 1000));
        }

        /* ── FPS log (every ~2 seconds) ── */
        int64_t now = esp_timer_get_time();
        if (now - fps_timer > 2000000) {
            int fps = (int)(frame_no * 1000000LL / (now - fps_timer));
            printf("GENESIS PROF (%d frames, %d skip): CPU=%.1fms  VID=%.1fms  AUD=%.1fms  total=%.1fms  FPS=%d\n",
                   prof_cnt, prof_skip,
                   prof_cpu_acc / (prof_cnt * 1000.0f),
                   prof_vid_acc / (prof_cnt * 1000.0f),
                   prof_aud_acc / (prof_cnt * 1000.0f),
                   (prof_cpu_acc + prof_vid_acc + prof_aud_acc) / (prof_cnt * 1000.0f),
                   fps);
            prof_cpu_acc = prof_vid_acc = prof_aud_acc = 0;
            prof_cnt = prof_skip = 0;
            frame_no = 0;
            fps_timer = now;
        }
    }

    /* ── Shutdown ── */
    ESP_LOGI(TAG, "Exiting emulation loop");

    /* Signal video task to exit */
    {
        uint8_t *sentinel = (uint8_t *)1;
        xQueueOverwrite(vidQueue, &sentinel);
        for (int i = 0; i < 50 && videoTaskRunning; i++)
            vTaskDelay(pdMS_TO_TICKS(20));
    }

    /* Signal audio task to exit */
    {
        genesis_quit_flag = true;
        xTaskNotifyGive(audioTaskHandle);
        for (int i = 0; i < 50 && audioTaskRunning; i++)
            vTaskDelay(pdMS_TO_TICKS(20));
    }

    vQueueDelete(vidQueue);
    vidQueue = NULL;

    odroid_audio_terminate();

cleanup:
    for (int i = 0; i < 2; i++) {
        if (gen_fb[i]) { heap_caps_free(gen_fb[i]); gen_fb[i] = NULL; }
    }
    if (gen_rgb565) { heap_caps_free(gen_rgb565); gen_rgb565 = NULL; }
    for (int i = 0; i < AUD_QUEUE_DEPTH; i++) {
        if (audio_dma_buf[i]) { heap_caps_free(audio_dma_buf[i]); audio_dma_buf[i] = NULL; }
    }
    for (int b = 0; b < 2; b++) {
        if (s_sidebar_buf[b]) { heap_caps_free(s_sidebar_buf[b]); s_sidebar_buf[b] = NULL; }
    }
    if (ROM_DATA) { heap_caps_free(ROM_DATA); ROM_DATA = NULL; }

    genesis_free_core();

    ESP_LOGI(TAG, "Genesis emulator shutdown complete");
}
