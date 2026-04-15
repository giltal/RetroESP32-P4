/*
 * snes_run.c — Entry point for SNES (snes9x) emulator on ESP32-P4
 *
 * Loads a .smc/.sfc ROM from SD card into PSRAM, runs the snes9x emulator,
 * handles in-game menu, and returns to the launcher when the user exits.
 */

#include "snes_run.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "esp_heap_caps.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_log.h"

/* snes9x core headers */
#include "snes9x/snes9x.h"
#include "snes9x/soundux.h"
#include "snes9x/memmap.h"
#include "snes9x/apu.h"
#include "snes9x/display.h"
#include "snes9x/cpuexec.h"
#include "snes9x/gfx.h"
#include "snes9x/ppu.h"
#include "snes9x/fxemu.h"
#include "snes9x/fxinst.h"
#include "snes9x/save.h"

/* Odroid platform */
#include "odroid_settings.h"
#include "odroid_audio.h"
#include "odroid_input.h"
#include "odroid_system.h"
#include "odroid_display.h"
#include "odroid_sdcard.h"
#include "st7701_lcd.h"

static const char *TAG = "SNES_RUN";

/* ─── Configuration ─────────────────────────────────────────────── */
#define SNES_FB_W         256
#define SNES_FB_H         224
#define SNES_FB_H_EXT     239
#define AUDIO_SAMPLE_RATE 32000
#define AUDIO_FRAG_SIZE   512
#define TARGET_FPS        60

/* ─── Globals required by snes9x core ──────────────────────────── */
bool overclock_cycles = false;
int one_c = 4, slow_one_c = 5, two_c = 6;
extern SGFX GFX;

/* ─── Module state ─────────────────────────────────────────────── */
static volatile bool snes_quit_flag = false;
static volatile bool videoTaskRunning = false;
static volatile bool audioTaskRunning = false;

/* Forward declarations */
static void snes_blit_sidebar_buttons(void);

static uint16_t *snes_fb[2] = { NULL, NULL };  /* double-buffer for SNES output */
static uint8_t  *snes_subscreen = NULL;         /* dedicated SubScreen buffer */
static int snes_fb_write = 0;
static QueueHandle_t vidQueue;
static TaskHandle_t videoTaskHandle;

/* Audio offload — double-buffered audio, mixed and played on Core 1 */
static TaskHandle_t audioTaskHandle;

#define AUD_QUEUE_DEPTH 2
static int16_t *audio_dma_buf[AUD_QUEUE_DEPTH] = { NULL, NULL };

/* Audio buffer (mixing — used by audio task on Core 1) */
static int16_t *audio_buf = NULL;

/* ─── snes9x platform callbacks ─────────────────────────────────── */

/* Pristine copies of GFX buffer pointers — defined in gfx.c, used to
 * detect/repair corruption (Load-access-fault crash on DKC level entry). */
extern uint8_t *saved_ZBuffer;
extern uint8_t *saved_SubZBuffer;

/*
 * Allocate Z-buffers at 512-pixel width (not 256) so that SNES mode 5/6
 * rendering (RenderedScreenWidth = 512) cannot overrun the buffers.
 */
#define ZBUF_WIDTH  512

bool S9xInitDisplay(void)
{
    GFX.Pitch = SNES_FB_W * 2;
    GFX.ZPitch = SNES_FB_W;
    GFX.Screen = (uint8_t *)snes_fb[0];

    /* Dedicated SubScreen buffer — required for color math and OBJ processing.
     * gfx.c uses SubScreen as scratch for OBJ line data and as the color
     * addition/subtraction source. Sharing with Screen causes corruption. */
    GFX.SubScreen = snes_subscreen;

    GFX.ZBuffer = (uint8_t *)heap_caps_malloc(ZBUF_WIDTH * SNES_FB_H_EXT,
                                               MALLOC_CAP_SPIRAM);
    GFX.SubZBuffer = (uint8_t *)heap_caps_malloc(ZBUF_WIDTH * SNES_FB_H_EXT,
                                                  MALLOC_CAP_SPIRAM);
    if (!GFX.ZBuffer || !GFX.SubZBuffer) {
        ESP_LOGE(TAG, "Failed to allocate Z-buffers");
        return false;
    }

    /* Keep pristine copies so we can repair corruption later */
    saved_ZBuffer    = GFX.ZBuffer;
    saved_SubZBuffer = GFX.SubZBuffer;
    ESP_LOGI(TAG, "ZBuffer=%p  SubZBuffer=%p  (512×%d)",
             GFX.ZBuffer, GFX.SubZBuffer, SNES_FB_H_EXT);
    return true;
}

void S9xDeinitDisplay(void) {}

void S9xToggleSoundChannel(int32_t channel) {}

bool S9xReadMousePosition(int32_t which, int32_t *x, int32_t *y, uint32_t *buttons)
{
    return false;
}

bool S9xReadSuperScopePosition(int32_t *x, int32_t *y, uint32_t *buttons)
{
    return false;
}

bool JustifierOffscreen(void) { return true; }
void JustifierButtons(uint32_t *justifiers) { (void)justifiers; }

/* ─── Joypad mapping ────────────────────────────────────────────── */
/*
 * Native SNES controller → SNES button masks.
 * The USB adapter reports buttons in SNES label order (A/B/X/Y match the
 * physical SNES face buttons); L1/L2 both map to SNES L, R1/R2 to SNES R.
 * MENU and VOLUME are handled separately (touch shoulder virtual buttons).
 */
uint32_t S9xReadJoypad(int32_t port)
{
    if (port != 0) return 0;

    odroid_gamepad_state gp;
    odroid_input_gamepad_read(&gp);

    uint32_t joy = 0;

    if (gp.values[ODROID_INPUT_UP])     joy |= SNES_UP_MASK;
    if (gp.values[ODROID_INPUT_DOWN])   joy |= SNES_DOWN_MASK;
    if (gp.values[ODROID_INPUT_LEFT])   joy |= SNES_LEFT_MASK;
    if (gp.values[ODROID_INPUT_RIGHT])  joy |= SNES_RIGHT_MASK;

    if (gp.values[ODROID_INPUT_A])      joy |= SNES_A_MASK;
    if (gp.values[ODROID_INPUT_B])      joy |= SNES_B_MASK;
    if (gp.values[ODROID_INPUT_X])      joy |= SNES_X_MASK;
    if (gp.values[ODROID_INPUT_Y])      joy |= SNES_Y_MASK;

    if (gp.values[ODROID_INPUT_L])      joy |= SNES_TL_MASK;
    if (gp.values[ODROID_INPUT_R])      joy |= SNES_TR_MASK;

    if (gp.values[ODROID_INPUT_SELECT]) joy |= SNES_SELECT_MASK;
    if (gp.values[ODROID_INPUT_START])  joy |= SNES_START_MASK;

    return joy;
}

/* ─── Video Task ────────────────────────────────────────────────── */
/*
 * Runs on Core 1. Receives SNES framebuffers via queue, copies them
 * centered into the 320×240 LCD buffer, and pushes to the display.
 */
static void snes_video_task(void *arg)
{
    uint16_t *frame = NULL;
    videoTaskRunning = true;
    int sidebar_countdown = 2;  /* blit sidebar on first 2 frames, then stop */

    while (1) {
        xQueuePeek(vidQueue, &frame, portMAX_DELAY);

        if (frame == (uint16_t *)1) break;  /* Quit sentinel */

        /* In-place green expansion on the displayed framebuffer.
         * snes9x:  RRRRR GGGGG 0 BBBBB  (GREEN_SHIFT=6, 5-bit green)
         * target:  RRRRR GGGGGG BBBBB    (standard RGB565, 6-bit green)
         * Safe because snes9x is now rendering to the OTHER double-buffer.
         */
        int total = SNES_FB_W * SNES_FB_H;
        for (int i = 0; i < total; i++) {
            uint16_t px = frame[i];
            frame[i] = px | ((px >> 5) & 0x0020);
        }

        /* Direct 2× scale + 270° rotate — no 320×240 staging buffer */
        ili9341_write_frame_rgb565_custom(frame, SNES_FB_W, SNES_FB_H,
                                           2.0f, false);

        /* Draw sidebar buttons once after the first frame clears borders */
        if (sidebar_countdown > 0) {
            snes_blit_sidebar_buttons();
            sidebar_countdown--;
        }

        xQueueReceive(vidQueue, &frame, portMAX_DELAY);
    }

    videoTaskRunning = false;
    vTaskDelete(NULL);
}

/* ─── Audio Task ────────────────────────────────────────────────── */
/*
 * Runs on Core 1. Receives mixed audio buffers via queue and submits
 * them to I2S (blocking), keeping the emulation loop on Core 0 unblocked.
 */
static void snes_audio_task(void *arg)
{
    audioTaskRunning = true;
    int dma_idx = 0;
    /* Fractional sample accumulator — avoids drift from 32000/60 = 533.333...
     * Every 3rd frame we mix 534 instead of 533, keeping the buffer aligned. */
    int frac_acc = 0;

    while (1) {
        /* Wait for signal from emu loop that a frame is ready to mix */
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        if (snes_quit_flag) break;

        /* Compute exact number of samples for this frame */
        int remaining = AUDIO_SAMPLE_RATE / TARGET_FPS;
        frac_acc += AUDIO_SAMPLE_RATE % TARGET_FPS;  /* += 32000 % 60 = 20 */
        if (frac_acc >= TARGET_FPS) {
            frac_acc -= TARGET_FPS;
            remaining++;  /* mix 534 this frame instead of 533 */
        }

        /* Mix + submit audio on Core 1 — frees Core 0 from ~1ms work */
        while (remaining > 0) {
            int n = (remaining < AUDIO_FRAG_SIZE) ? remaining : AUDIO_FRAG_SIZE;
            int16_t *dest = audio_dma_buf[dma_idx];
            S9xMixSamples(dest, n * 2);  /* stereo: n*2 int16_t */
            odroid_audio_submit(dest, n);
            dma_idx = (dma_idx + 1) % AUD_QUEUE_DEPTH;
            remaining -= n;
        }
    }

    audioTaskRunning = false;
    vTaskDelete(NULL);
}

/* ─── In-game menu ──────────────────────────────────────────────── */
/* Standard RGB565 color constants (byte_swap=false like SMS/NES path) */
#define DCOL_BLACK  0x0000
#define DCOL_WHITE  0xFFFF
#define DCOL_GREEN  0x07E0   /* standard RGB565 green */
#define DCOL_RED    0xF800   /* standard RGB565 red   */
#define DCOL_YELLOW 0xFFE0   /* standard RGB565 yellow */

/* Minimal 5×5 bitmap font for menu labels (A-Z, space) */
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

static void menu_draw_char(uint16_t *fb, int cx, int cy, char ch, uint16_t color)
{
    int idx = -1;
    if (ch >= 'A' && ch <= 'Z') idx = ch - 'A';
    else if (ch >= 'a' && ch <= 'z') idx = ch - 'a';
    if (idx < 0) return;  /* space or unknown → skip */
    for (int row = 0; row < 5; row++)
        for (int col = 0; col < 5; col++)
            if (font5x5[idx][row] & (0x10 >> col))
                if ((cy + row) < SNES_FB_H && (cx + col) < SNES_FB_W)
                    fb[(cy + row) * SNES_FB_W + (cx + col)] = color;
}

static void menu_draw_str(uint16_t *fb, int x, int y, const char *s, uint16_t color)
{
    while (*s) { menu_draw_char(fb, x, y, *s++, color); x += 6; }
}

/* ─── Save state helpers ───────────────────────────────────────── */

static char *snes_get_save_path(void)
{
    char *romName = odroid_settings_RomFilePath_get();
    if (!romName) return NULL;
    char *fileName = odroid_util_GetFileNameWithoutExtension(romName);
    free(romName);
    if (!fileName) return NULL;
    char pathBuf[256];
    snprintf(pathBuf, sizeof(pathBuf), "/sd/odroid/data/snes/%s.sav", fileName);
    free(fileName);
    return strdup(pathBuf);
}

static char *snes_get_sram_path(void)
{
    char *romName = odroid_settings_RomFilePath_get();
    if (!romName) return NULL;
    char *fileName = odroid_util_GetFileNameWithoutExtension(romName);
    free(romName);
    if (!fileName) return NULL;
    char pathBuf[256];
    snprintf(pathBuf, sizeof(pathBuf), "/sd/odroid/data/snes/%s.srm", fileName);
    free(fileName);
    return strdup(pathBuf);
}

static void snes_load_sram(void)
{
    if (Memory.SRAMSize == 0) return;  /* ROM has no battery save */

    char *path = snes_get_sram_path();
    if (!path) return;

    int sram_bytes = (Memory.SRAMMask + 1);
    if (sram_bytes <= 0 || sram_bytes > 0x20000) { free(path); return; }

    esp_err_t r = odroid_sdcard_open("/sd");
    if (r != ESP_OK) { free(path); return; }

    FILE *f = fopen(path, "rb");
    if (f) {
        size_t n = fread(Memory.SRAM, 1, sram_bytes, f);
        fclose(f);
        ESP_LOGI(TAG, "SRAM loaded: %d bytes from %s", (int)n, path);
    } else {
        ESP_LOGI(TAG, "No SRAM file found: %s (new game)", path);
    }

    odroid_sdcard_close();
    free(path);
}

static void snes_save_sram(void)
{
    if (Memory.SRAMSize == 0) return;  /* ROM has no battery save */

    char *path = snes_get_sram_path();
    if (!path) return;

    int sram_bytes = (Memory.SRAMMask + 1);
    if (sram_bytes <= 0 || sram_bytes > 0x20000) { free(path); return; }

    esp_err_t r = odroid_sdcard_open("/sd");
    if (r != ESP_OK) { free(path); return; }

    mkdir("/sd/odroid", 0775);
    mkdir("/sd/odroid/data", 0775);
    mkdir("/sd/odroid/data/snes", 0775);

    FILE *f = fopen(path, "wb");
    if (f) {
        size_t n = fwrite(Memory.SRAM, 1, sram_bytes, f);
        fclose(f);
        ESP_LOGI(TAG, "SRAM saved: %d bytes to %s", (int)n, path);
    } else {
        ESP_LOGE(TAG, "Failed to write SRAM: %s", path);
    }

    odroid_sdcard_close();
    free(path);
}

static bool snes_check_save_exists(void)
{
    char *path = snes_get_save_path();
    if (!path) return false;
    struct stat st;
    bool exists = (stat(path, &st) == 0);
    free(path);
    return exists;
}

static bool snes_do_save_state(void)
{
    char *path = snes_get_save_path();
    if (!path) return false;

    esp_err_t r = odroid_sdcard_open("/sd");
    if (r != ESP_OK) { free(path); return false; }

    mkdir("/sd/odroid", 0775);
    mkdir("/sd/odroid/data", 0775);
    mkdir("/sd/odroid/data/snes", 0775);

    bool ok = S9xSaveState(path);
    free(path);
    odroid_sdcard_close();
    return ok;
}

static bool snes_do_load_state(void)
{
    char *path = snes_get_save_path();
    if (!path) return false;

    esp_err_t r = odroid_sdcard_open("/sd");
    if (r != ESP_OK) { free(path); return false; }

    bool ok = S9xLoadState(path);
    free(path);
    odroid_sdcard_close();
    return ok;
}

/* ─── Volume overlay ───────────────────────────────────────────── */
static void snes_show_volume(void)
{
    static const char * const level_names[] = { "MUTE", "25%", "50%", "75%", "100%" };

    int level = (int)odroid_audio_volume_get();
    /* Cycle to next level on press */
    level = (level + 1) % ODROID_VOLUME_LEVEL_COUNT;
    odroid_audio_volume_set(level);
    odroid_settings_Volume_set(level);

    int timeout = 25;  /* ~2 s at 80 ms per poll */

    uint16_t *fb = snes_fb[snes_fb_write];
    if (!fb) return;

    odroid_gamepad_state prev;
    odroid_input_gamepad_read(&prev);

    /* Debounce: wait for volume button release */
    for (int i = 0; i < 100; i++) {
        odroid_gamepad_state tmp;
        odroid_input_gamepad_read(&tmp);
        if (!tmp.values[ODROID_INPUT_VOLUME]) break;
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    odroid_input_gamepad_read(&prev);

    while (timeout > 0) {
        /* Draw overlay on the write framebuffer */
        int box_w = 140, box_h = 34;
        int box_x = (SNES_FB_W - box_w) / 2;
        int box_y = 4;

        /* Dark background + border */
        for (int r = box_y; r < box_y + box_h && r < SNES_FB_H; r++)
            for (int c = box_x; c < box_x + box_w && c < SNES_FB_W; c++)
                fb[r * SNES_FB_W + c] = DCOL_BLACK;
        for (int c = box_x; c < box_x + box_w; c++) {
            if (box_y < SNES_FB_H) fb[box_y * SNES_FB_W + c] = DCOL_WHITE;
            if (box_y + box_h - 1 < SNES_FB_H) fb[(box_y + box_h - 1) * SNES_FB_W + c] = DCOL_WHITE;
        }
        for (int r = box_y; r < box_y + box_h && r < SNES_FB_H; r++) {
            fb[r * SNES_FB_W + box_x] = DCOL_WHITE;
            fb[r * SNES_FB_W + box_x + box_w - 1] = DCOL_WHITE;
        }

        /* Title */
        char title[32];
        snprintf(title, sizeof(title), "VOL %s", level_names[level]);
        menu_draw_str(fb, box_x + 8, box_y + 4, title, DCOL_YELLOW);

        /* Volume bar */
        int bar_x = box_x + 6;
        int bar_y = box_y + 16;
        int bar_w = box_w - 12;
        int bar_h = 10;
        for (int r = bar_y; r < bar_y + bar_h && r < SNES_FB_H; r++)
            for (int c = bar_x; c < bar_x + bar_w && c < SNES_FB_W; c++)
                fb[r * SNES_FB_W + c] = 0x2104; /* dark grey */
        if (level > 0) {
            int fill = (bar_w * level) / (ODROID_VOLUME_LEVEL_COUNT - 1);
            uint16_t bar_col = (level <= 1) ? 0x07E0 : (level <= 3) ? 0x07FF : DCOL_YELLOW;
            for (int r = bar_y; r < bar_y + bar_h && r < SNES_FB_H; r++)
                for (int c = bar_x; c < bar_x + fill && c < SNES_FB_W; c++)
                    fb[r * SNES_FB_W + c] = bar_col;
        }

        ili9341_write_frame_rgb565_custom(fb, SNES_FB_W, SNES_FB_H, 2.0f, false);

        vTaskDelay(pdMS_TO_TICKS(80));
        odroid_gamepad_state state;
        odroid_input_gamepad_read(&state);

        /* Left/right adjust volume while overlay is visible */
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
        /* Another press of volume cycles forward */
        if (state.values[ODROID_INPUT_VOLUME] && !prev.values[ODROID_INPUT_VOLUME]) {
            level = (level + 1) % ODROID_VOLUME_LEVEL_COUNT;
            odroid_audio_volume_set(level);
            odroid_settings_Volume_set(level);
            timeout = 25;
        }
        /* A or B dismisses immediately */
        if ((state.values[ODROID_INPUT_A] && !prev.values[ODROID_INPUT_A]) ||
            (state.values[ODROID_INPUT_B] && !prev.values[ODROID_INPUT_B])) {
            break;
        }

        prev = state;
        timeout--;
    }
}

static bool snes_show_menu(void)
{
    /* Use the inactive framebuffer as scratch for menu drawing */
    uint16_t *fb = snes_fb[snes_fb_write];
    if (!fb) return false;

    int sel = 0;
    bool has_save = snes_check_save_exists();
    const int ITEMS = 4;
    const char *labels[] = { "RESUME", "SAVE STATE", "LOAD STATE", "EXIT GAME" };

    odroid_gamepad_state prev;
    odroid_input_gamepad_read(&prev);

    /* Debounce menu button */
    for (int i = 0; i < 50; i++) {
        odroid_gamepad_state tmp;
        odroid_input_gamepad_read(&tmp);
        if (!tmp.values[ODROID_INPUT_MENU]) break;
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    /* Status message shown after save/load attempt */
    const char *status_msg = NULL;
    uint16_t status_color = DCOL_GREEN;
    int status_timeout = 0;

    while (1) {
        int box_w = 140, box_h = 18 + ITEMS * 14;
        int box_x = (SNES_FB_W - box_w) / 2;
        int box_y = (SNES_FB_H - box_h) / 2;

        /* Background box */
        for (int r = box_y; r < box_y + box_h && r < SNES_FB_H; r++)
            for (int c = box_x; c < box_x + box_w && c < SNES_FB_W; c++)
                fb[r * SNES_FB_W + c] = DCOL_BLACK;

        /* Border */
        for (int c = box_x; c < box_x + box_w; c++) {
            fb[box_y * SNES_FB_W + c] = DCOL_WHITE;
            fb[(box_y + box_h - 1) * SNES_FB_W + c] = DCOL_WHITE;
        }
        for (int r = box_y; r < box_y + box_h; r++) {
            fb[r * SNES_FB_W + box_x] = DCOL_WHITE;
            fb[r * SNES_FB_W + box_x + box_w - 1] = DCOL_WHITE;
        }

        /* Menu items with text */
        for (int i = 0; i < ITEMS; i++) {
            /* Grey out "LOAD STATE" if no save file exists */
            bool greyed = (i == 2 && !has_save);
            uint16_t color = greyed ? 0x4208 : (i == sel) ? DCOL_GREEN : DCOL_WHITE;
            int ty = box_y + 10 + i * 14;
            int tx = box_x + 20;

            /* Selection arrow ">" */
            if (i == sel) {
                for (int dy = 0; dy < 5; dy++)
                    for (int dx = 0; dx < 3; dx++)
                        fb[(ty + dy) * SNES_FB_W + tx - 10 + dx] = color;
            }

            menu_draw_str(fb, tx, ty, labels[i], color);
        }

        /* Draw status message if active */
        if (status_msg && status_timeout > 0) {
            menu_draw_str(fb, box_x + 8, box_y + box_h + 4, status_msg, status_color);
            status_timeout--;
        }

        ili9341_write_frame_rgb565_custom(fb, SNES_FB_W, SNES_FB_H,
                                          2.0f, false);

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
                if (snes_do_save_state()) {
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
                if (snes_do_load_state()) {
                    return false;  /* Resume with loaded state */
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
/*
 * Draw "MENU" and "VOL" button labels in the black side bars.
 *
 * LCD portrait: 480 wide (x) × 800 tall (y).
 * Device held landscape with portrait-top on LEFT:
 *   landscape_x = portrait_y          (small portrait_y → landscape LEFT)
 *   landscape_y = 479 − portrait_x    (small portrait_x → landscape BOTTOM)
 *
 * Touch zone: portrait y < 170 = landscape LEFT  = MENU
 *             portrait y > 630 = landscape RIGHT = VOL
 *
 * SNES game area on LCD: portrait (16, 144) to (463, 655).
 *   LEFT sidebar  = portrait y   0..143  → MENU
 *   RIGHT sidebar = portrait y 656..799  → VOL
 *
 * Glyph rendering (upright text, reading L→R in landscape):
 *   font_col (left=0..right=4)    → portrait_y INCREASES (landscape L→R)
 *   font_row (top=0..bottom=4)    → portrait_x DECREASES (landscape top→bottom)
 *   Characters march in portrait +y direction (= landscape L→R).
 */

/* Persistent sidebar button buffers — allocated once, reused every frame */
static uint16_t *s_sidebar_buf[2] = { NULL, NULL };
static const struct { const char *text; int px, py, pw, ph; } s_sidebar_btns[] = {
    { "MENU", 200, 22,  80, 100 },       /* landscape LEFT  sidebar (portrait y < 144) */
    { "VOL",  200, 700, 80,  84 },       /* landscape RIGHT sidebar (portrait y > 656) */
};

static void snes_init_sidebar_buttons(void)
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

        /* Background fill */
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

        /* Render upright text reading L→R in landscape */
        const char *s = s_sidebar_btns[b].text;
        int nch = 0;
        for (const char *p = s; *p; p++) nch++;
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

        ESP_LOGI(TAG, "Sidebar btn[%d] '%s' rendered, portrait (%d,%d) %dx%d",
                 b, s_sidebar_btns[b].text, s_sidebar_btns[b].px, s_sidebar_btns[b].py, pw, ph);
    }
}

/* Blit pre-rendered sidebar buttons to LCD (called from video task AFTER each game frame).
 * Uses direct CPU memcpy to the DPI framebuffer, bypassing the async DMA2D
 * pipeline to avoid contention with the game-frame draw_bitmap (which holds
 * a non-blocking semaphore while its DMA transfer is in-flight). */
static void snes_blit_sidebar_buttons(void)
{
    for (int b = 0; b < 2; b++) {
        if (!s_sidebar_buf[b]) continue;
        st7701_lcd_draw_to_fb(
            (uint16_t)s_sidebar_btns[b].px, (uint16_t)s_sidebar_btns[b].py,
            (uint16_t)s_sidebar_btns[b].pw, (uint16_t)s_sidebar_btns[b].ph,
            s_sidebar_buf[b]);
    }
}

/* ─── Main emulation logic ──────────────────────────────────────── */

static void snes_init_core(void)
{
    Settings.CyclesPercentage = 100;
    Settings.H_Max = SNES_CYCLES_PER_SCANLINE;
    Settings.FrameTimePAL = 20000;
    Settings.FrameTimeNTSC = 16667;
    Settings.ControllerOption = SNES_JOYPAD;
    Settings.HBlankStart = (256 * Settings.H_Max) / SNES_HCOUNTER_MAX;
    Settings.SoundPlaybackRate = AUDIO_SAMPLE_RATE;
    Settings.DisableSoundEcho = true;   /* skip echo FIR filter — removes metallic artefact */
    Settings.InterpolatedSound = false; /* cheaper mixing, avoids hi-freq ringing */

    if (!S9xInitMemory()) {
        ESP_LOGE(TAG, "S9xInitMemory failed");
        return;
    }
    if (!S9xInitAPU()) {
        ESP_LOGE(TAG, "S9xInitAPU failed");
        return;
    }
    if (!S9xInitSound(0, 0)) {
        ESP_LOGE(TAG, "S9xInitSound failed");
        return;
    }

    /* S9xInitDisplay MUST be called before S9xInitGFX so that
       GFX.Screen, GFX.SubScreen, GFX.Pitch, ZBuffer etc. are
       set before S9xInitGFX reads them to compute GFX.Delta. */
    if (!S9xInitDisplay()) {
        ESP_LOGE(TAG, "S9xInitDisplay failed");
        return;
    }

    if (!S9xInitGFX()) {
        ESP_LOGE(TAG, "S9xInitGFX failed");
        return;
    }

    S9xSetPlaybackRate(Settings.SoundPlaybackRate);

    ESP_LOGI(TAG, "snes9x core initialized");
}

void snes_run(const char *rom_path)
{
    ESP_LOGI(TAG, "Starting SNES emulator, ROM=%s", rom_path);

    snes_quit_flag = false;

    /* ── Allocate buffers ── */
    /* SNES double-buffer framebuffers — allocate in PSRAM */
    for (int i = 0; i < 2; i++) {
        snes_fb[i] = (uint16_t *)heap_caps_calloc(1,
            SNES_FB_W * SNES_FB_H_EXT * sizeof(uint16_t),
            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!snes_fb[i]) {
            ESP_LOGE(TAG, "Failed to allocate SNES FB %d", i);
            return;
        }
    }
    snes_fb_write = 0;

    /* SubScreen buffer — needed for color math and OBJ processing */
    snes_subscreen = (uint8_t *)heap_caps_calloc(1,
        SNES_FB_W * SNES_FB_H_EXT * sizeof(uint16_t),
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!snes_subscreen) {
        ESP_LOGE(TAG, "Failed to allocate SubScreen buffer");
        return;
    }

    /* Audio buffer (mixing side — used by audio task on Core 1) */
    audio_buf = (int16_t *)heap_caps_malloc(AUDIO_FRAG_SIZE * 4,
        MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!audio_buf) {
        ESP_LOGE(TAG, "Failed to allocate audio buffer");
        goto cleanup;
    }

    /* Audio DMA double-buffers — sent to audio task on Core 1 */
    for (int i = 0; i < AUD_QUEUE_DEPTH; i++) {
        audio_dma_buf[i] = (int16_t *)heap_caps_malloc(AUDIO_FRAG_SIZE * 4,
            MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        if (!audio_dma_buf[i]) {
            ESP_LOGE(TAG, "Failed to allocate audio DMA buffer %d", i);
            goto cleanup;
        }
    }

    /* ── Initialize snes9x core ── */
    snes_init_core();

    /* Verify critical GFX buffers were allocated — snes_init_core returns void */
    if (!GFX.ZBuffer || !GFX.SubZBuffer || !Memory.VRAM) {
        ESP_LOGE(TAG, "Core init failed: ZBuffer=%p SubZBuffer=%p VRAM=%p",
                 GFX.ZBuffer, GFX.SubZBuffer, Memory.VRAM);
        goto cleanup;
    }

    /* ── Load ROM from SD card ── */
    esp_err_t r = odroid_sdcard_open("/sd");
    if (r != ESP_OK) {
        ESP_LOGE(TAG, "SD card open failed (%d)", (int)r);
        goto cleanup;
    }

    /* Read ROM file into Memory.ROM (allocated by S9xInitMemory) */
    {
        /* S9xInitMemory allocates Memory.ROM but doesn't set ROM_Size.
         * We need to allocate enough space for the ROM. */
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

        /* Memory.ROM was allocated by S9xInitMemory — check it */
        if (!Memory.ROM) {
            Memory.ROM = (uint8_t *)heap_caps_calloc(1, MAX_ROM_SIZE,
                MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            if (!Memory.ROM) {
                ESP_LOGE(TAG, "Failed to allocate ROM buffer");
                fclose(f);
                odroid_sdcard_close();
                goto cleanup;
            }
        }

        size_t bytes_read = fread(Memory.ROM, 1, rom_size, f);
        fclose(f);

        /* Skip 512-byte SMC copier header if present.
         * LoadROM() detects it but the memmove is commented out in this
         * fork, so we strip it here before calling LoadROM(). */
        if ((bytes_read & 0x7FF) == 512) {
            ESP_LOGI(TAG, "Stripping 512-byte SMC header");
            memmove(Memory.ROM, Memory.ROM + 512, bytes_read - 512);
            bytes_read -= 512;
        }

        Memory.ROM_Size = bytes_read;

        ESP_LOGI(TAG, "ROM loaded: %d bytes", (int)bytes_read);
    }

    odroid_sdcard_close();

    /* Initialize ROM (detect type, apply fixes, etc.) */
    if (!LoadROM(NULL)) {
        ESP_LOGE(TAG, "LoadROM failed — ROM format not recognized");
        goto cleanup;
    }

    ESP_LOGI(TAG, "ROM: %s (%s, %s)", Memory.ROMName,
             Memory.HiROM ? "HiROM" : "LoROM",
             Settings.PAL ? "PAL" : "NTSC");
    ESP_LOGI(TAG, "ROMType=0x%02X, SuperFX=%d", Memory.ROMType, Settings.SuperFX);

    /* ── SuperFX / GSU co-processor init ── */
    if (Settings.SuperFX) {
        ESP_LOGI(TAG, "SuperFX ROM detected — using Memory.SRAM as GSU RAM");
        ESP_LOGI(TAG, "Pre-init SRAMSize=%d SRAMMask=0x%lX",
                 Memory.SRAMSize, (unsigned long)Memory.SRAMMask);
        /* Use Memory.SRAM (128KB, already allocated) as GSU RAM.
         * CPU accesses banks $60-$7D via MAP_LOROM_SRAM → Memory.SRAM.
         * GSU accesses via pvRam → also Memory.SRAM.
         * Both CPU and GSU share the same physical buffer. */
        S9xInitSuperFX();
        SuperFX.pvRam = Memory.SRAM;
        SuperFX.nRamBanks = 2;  /* 128KB = 2 x 64KB banks */
        /* Ensure SRAMMask covers full 128KB for GSU bank access */
        if (Memory.SRAMMask < 0x1FFFF)
            Memory.SRAMMask = 0x1FFFF;  /* 128KB-1 */
        S9xResetSuperFX();
        /* Ensure GSU.pvRam matches after reset */
        GSU.pvRam = SuperFX.pvRam;
        GSU.nRamBanks = SuperFX.nRamBanks;
        ESP_LOGI(TAG, "SuperFX initialized, %d KB GSU RAM (shared SRAM), SRAMMask=0x%lX",
                 SuperFX.nRamBanks * 64, (unsigned long)Memory.SRAMMask);
        ESP_LOGI(TAG, "FillRAM[3030]=%02X [3031]=%02X [303B]=%02X after reset",
                 Memory.FillRAM[0x3030], Memory.FillRAM[0x3031], Memory.FillRAM[0x303B]);
    }

    /* S9xInitDisplay already called inside snes_init_core() */

    /* ── Load SRAM (battery saves) from SD — skip for SuperFX (uses SRAM as GSU RAM) ── */
    if (!Settings.SuperFX) {
        snes_load_sram();
    }

    /* ── Initialize audio ── */
    odroid_audio_init(AUDIO_SAMPLE_RATE);

    /* ── Pre-render sidebar button bitmaps ── */
    snes_init_sidebar_buttons();

    /* SNES has native X/Y face buttons — don't alias them to Menu/Volume */
    odroid_input_xy_menu_disable = true;

    /* ── Create video queue and task ── */
    vidQueue = xQueueCreate(1, sizeof(uint16_t *));
    xTaskCreatePinnedToCore(snes_video_task, "snes_video", 4096,
                            NULL, 5, &videoTaskHandle, 1);

    /* ── Create audio task (Core 1, signal-based) ── */
    xTaskCreatePinnedToCore(snes_audio_task, "snes_audio", 4096,
                            NULL, 6, &audioTaskHandle, 1);

    /* ── Auto-resume: load save state if launcher requested it ── */
    if (odroid_settings_StartAction_get() == ODROID_START_ACTION_RESTART) {
        odroid_settings_StartAction_set(ODROID_START_ACTION_NORMAL);
        if (snes_check_save_exists()) {
            ESP_LOGI(TAG, "Auto-resuming from save state");
            snes_do_load_state();
        }
    }

    /* ── Emulation loop ── */
    int frame_no = 0;
    int64_t fps_timer = esp_timer_get_time();
    int32_t framedrop_balance = 0;
    int64_t frame_timer = esp_timer_get_time();
    const int64_t target_frame_us = Settings.PAL ? 20000 : 16667;

    IPPU.RenderThisFrame = true;

    ESP_LOGI(TAG, "Entering emulation loop (%s, target %lld us/frame)",
             Settings.PAL ? "PAL" : "NTSC", target_frame_us);
    if (Settings.SuperFX) {
        ESP_LOGI(TAG, "PRE-LOOP FillRAM[3030]=%02X [3031]=%02X [303B]=%02X",
                 Memory.FillRAM[0x3030], Memory.FillRAM[0x3031], Memory.FillRAM[0x303B]);
    }

    /* Profiling accumulators */
    int64_t prof_cpu_acc = 0, prof_audio_acc = 0, prof_vid_acc = 0;
    int prof_cnt = 0, prof_skip = 0;

    odroid_gamepad_state gp_prev;
    odroid_input_gamepad_read(&gp_prev);

    while (!snes_quit_flag) {
        /* Check for menu / volume button (edge-triggered) */
        odroid_gamepad_state gp;
        odroid_input_gamepad_read(&gp);

        if (gp.values[ODROID_INPUT_VOLUME] && !gp_prev.values[ODROID_INPUT_VOLUME]) {
            snes_show_volume();
            fps_timer = esp_timer_get_time();
            frame_timer = esp_timer_get_time();
            framedrop_balance = 0;
        }

        if (gp.values[ODROID_INPUT_MENU] && !gp_prev.values[ODROID_INPUT_MENU]) {
            if (snes_show_menu()) {
                snes_quit_flag = true;
                break;
            }
            /* After resume, re-seed timers */
            fps_timer = esp_timer_get_time();
            frame_timer = esp_timer_get_time();
            framedrop_balance = 0;
            frame_no = 0;
        }

        gp_prev = gp;

        /* Run one SNES frame */
        int64_t t_cpu0 = esp_timer_get_time();
        S9xMainLoop();
        int64_t t_cpu1 = esp_timer_get_time();

        /* Audio — signal Core 1 audio task to mix and play */
        xTaskNotifyGive(audioTaskHandle);
        int64_t t_aud1 = esp_timer_get_time();

        /* Video — send frame to display task */
        if (IPPU.RenderThisFrame) {
            int next = snes_fb_write ^ 1;
            void *arg = (void *)snes_fb[snes_fb_write];
            xQueueOverwrite(vidQueue, &arg);
            snes_fb_write = next;

            /* Point snes9x to the other buffer */
            GFX.Screen = (uint8_t *)snes_fb[snes_fb_write];
            /* SubScreen stays at its dedicated buffer — do NOT alias to Screen */
        }
        int64_t t_vid1 = esp_timer_get_time();

        /* Accumulate profiling */
        prof_cpu_acc   += (t_cpu1 - t_cpu0);
        prof_audio_acc += (t_aud1 - t_cpu1);
        prof_vid_acc   += (t_vid1 - t_aud1);
        prof_cnt++;
        if (!IPPU.RenderThisFrame) prof_skip++;

        /* Frame-drop logic */
        IPPU.RenderThisFrame = true;
        int64_t now = esp_timer_get_time();
        framedrop_balance += (int32_t)((now - frame_timer) - target_frame_us);
        frame_timer = now;

        if (framedrop_balance < 1000)
            framedrop_balance = 0;

        if (framedrop_balance > target_frame_us) {
            IPPU.RenderThisFrame = false;
            framedrop_balance -= target_frame_us;
        }

        /* FPS + profiling log (every ~2 seconds) */
        frame_no++;
        if (now - fps_timer > 2000000) {
            int fps = (int)(frame_no * 1000000LL / (now - fps_timer));
            printf("SNES PROF (%d frames, %d skip): CPU=%.1fms  AUD=%.1fms  VID=%.1fms  total=%.1fms  FPS=%d\n",
                   prof_cnt, prof_skip,
                   prof_cpu_acc / (prof_cnt * 1000.0f),
                   prof_audio_acc / (prof_cnt * 1000.0f),
                   prof_vid_acc / (prof_cnt * 1000.0f),
                   (prof_cpu_acc + prof_audio_acc + prof_vid_acc) / (prof_cnt * 1000.0f),
                   fps);
            if (Settings.SuperFX) {
                printf("  SFX: GO=%d SFR=0x%04X R15=0x%04lX PBR=%lu R12=0x%04lX R13=0x%04lX PC=%02X:%04X Bright=%d Blank=%d\n",
                       (Memory.FillRAM[0x3030] & 0x20) ? 1 : 0,
                       Memory.FillRAM[0x3030] | (Memory.FillRAM[0x3031] << 8),
                       (unsigned long)GSU.avReg[15], (unsigned long)GSU.vPrgBankReg,
                       (unsigned long)GSU.avReg[12], (unsigned long)GSU.avReg[13],
                       ICPU.Registers.PB, ICPU.Registers.PC,
                       PPU.Brightness, PPU.ForcedBlanking);
                /* One-time diagnostics */
                {
                    static int dump_count = 0;
                    if (dump_count < 5) {
                        dump_count++;
                        uint8_t nmitimen = Memory.FillRAM[0x4200];
                        printf("  NMITIMEN=$%02X NMI=%s SP=$%04X DB=%02X\n",
                            nmitimen, (nmitimen & 0x80) ? "ON" : "OFF",
                            ICPU.Registers.S.W, ICPU.Registers.DB);
                        /* Dump SRAM around $4CB0 (what the game's loop reads at $704CB4) */
                        printf("  SRAM@4CB0: ");
                        for (int i = 0; i < 32; i++)
                            printf("%02X", Memory.SRAM[0x4CB0 + i]);
                        printf("\n");
                        /* Dump first 32 bytes of SRAM to see if anything got written */
                        printf("  SRAM@0000: ");
                        for (int i = 0; i < 32; i++)
                            printf("%02X", Memory.SRAM[i]);
                        printf("\n");
                        /* Key SFX control regs */
                        printf("  SFX regs: SCMR=%02X RAMBR=%02X CFGR=%02X CLSR=%02X SCBR=%02X\n",
                            Memory.FillRAM[0x303A], Memory.FillRAM[0x303C],
                            Memory.FillRAM[0x3037], Memory.FillRAM[0x3039],
                            Memory.FillRAM[0x3038]);
                        /* Dump raw ROM around $553A (GSU code at $D53A mirror) */
                        printf("  ROM@553A: ");
                        for (int i = 0; i < 32; i++)
                            printf("%02X ", Memory.ROM[0x553A + i]);
                        printf("\n");
                        printf("  SRAMSize=%d SRAMMask=0x%lX\n",
                            Memory.SRAMSize, (unsigned long)Memory.SRAMMask);
                    }
                }
            }
            prof_cpu_acc = prof_audio_acc = prof_vid_acc = 0;
            prof_cnt = prof_skip = 0;
            frame_no = 0;
            fps_timer = now;
        }
    }

    /* ── Shutdown ── */
    ESP_LOGI(TAG, "Exiting emulation loop");

    /* Save SRAM (battery saves) to SD before cleanup — skip for SuperFX */
    if (!Settings.SuperFX) {
        snes_save_sram();
    }

    /* Signal video task to exit */
    {
        uint16_t *sentinel = (uint16_t *)1;
        xQueueOverwrite(vidQueue, &sentinel);
        for (int i = 0; i < 50 && videoTaskRunning; i++)
            vTaskDelay(pdMS_TO_TICKS(20));
    }

    /* Signal audio task to exit */
    {
        xTaskNotifyGive(audioTaskHandle);  /* snes_quit_flag is set, task will exit */
        for (int i = 0; i < 50 && audioTaskRunning; i++)
            vTaskDelay(pdMS_TO_TICKS(20));
    }

    vQueueDelete(vidQueue);
    vidQueue = NULL;

    odroid_audio_terminate();
    S9xDeinitGFX();
    S9xDeinitAPU();
    S9xDeinitMemory();

cleanup:
    for (int i = 0; i < 2; i++) {
        if (snes_fb[i]) { heap_caps_free(snes_fb[i]); snes_fb[i] = NULL; }
    }
    if (snes_subscreen) { heap_caps_free(snes_subscreen); snes_subscreen = NULL; }
    if (audio_buf) { heap_caps_free(audio_buf); audio_buf = NULL; }
    for (int i = 0; i < AUD_QUEUE_DEPTH; i++) {
        if (audio_dma_buf[i]) { heap_caps_free(audio_dma_buf[i]); audio_dma_buf[i] = NULL; }
    }
    if (GFX.ZBuffer) { heap_caps_free(GFX.ZBuffer); GFX.ZBuffer = NULL; }
    if (GFX.SubZBuffer) { heap_caps_free(GFX.SubZBuffer); GFX.SubZBuffer = NULL; }
    saved_ZBuffer = NULL;
    saved_SubZBuffer = NULL;

    ESP_LOGI(TAG, "SNES emulator shutdown complete");
}
