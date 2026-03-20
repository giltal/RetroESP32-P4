/*
 * stella_run.cpp — Atari 2600 (Stella) emulator wrapper for ESP32-P4
 *
 * Provides stella_run(rom_path) callable from C launcher.
 * Manages video task, in-game menu, save/load, volume overlay.
 */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"

extern "C" {
#include "odroid_settings.h"
#include "odroid_audio.h"
#include "odroid_input.h"
#include "odroid_system.h"
#include "odroid_display.h"
}

// Stella
#include "Console.hxx"
#include "Cart.hxx"
#include "Props.hxx"
#include "MD5.hxx"
#include "Sound.hxx"
#include "OSystem.hxx"
#include "TIA.hxx"
#include "PropsSet.hxx"
#include "Switches.hxx"
#include "SoundSDL.hxx"
#include "Control.hxx"
#include "Event.hxx"
#include "Paddles.hxx"
#include "Serializer.hxx"

#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <sys/stat.h>
#include <string>
using std::string;

#include "stella_run.h"

/* ===================================================================
 * Constants
 * =================================================================== */
#define AUDIO_SAMPLE_RATE  31400
#define STELLA_WIDTH       160
#define STELLA_HEIGHT      250
#define DISPLAY_WIDTH      320
#define DISPLAY_HEIGHT     240

/* Paddle ADC constants */
#define PADDLE_ADC_LO       100   /* ADC low clamp (avoid noise floor) */
#define PADDLE_ADC_HI       4000  /* ADC clamp: pot at 3.3V end (avoid ceiling noise) */
#define PADDLE_DETECT_SPREAD 300  /* max ADC spread for pot detection */

static bool st_paddle_adc_enabled = false;

/* ===================================================================
 * Globals — prefixed to avoid collision with other emulators
 * =================================================================== */
/* RenderFlag: checked by TIA.cpp to skip rendering on non-draw frames */
bool RenderFlag = true;

static QueueHandle_t st_vidQueue = NULL;
static TaskHandle_t  st_videoTaskHandle = NULL;
static volatile bool st_videoTaskIsRunning = false;
static volatile bool st_exitRequested = false;
static volatile bool st_menu_request = false;

static Console *st_console = NULL;
static Cartridge *st_cartridge = NULL;
static Settings *st_settings = NULL;
static OSystem *st_osystem = NULL;
static uint32_t st_tiaSamplesPerFrame = 0;

static uint8_t  st_framebuffer[STELLA_WIDTH * STELLA_HEIGHT];
static uint8_t  st_prev_framebuffer[STELLA_WIDTH * STELLA_HEIGHT];
static uint16_t st_pal16[256];
static bool     st_isPal = false;
static uint16_t *st_displayBuffer = NULL;  /* 320×240 RGB565 */
static int32_t  *st_sampleBuffer = NULL;

/* Save path */
static char st_save_path[256] = "";

/* Volume overlay */
static int      volume_level = 3;   /* 0=mute,1=25%,2=50%,3=75%,4=100% */
static int      volume_show_frames = 0;

/* FPS counter */
static uint32_t fps_frame_count = 0;
static int64_t  fps_last_time = 0;
static float    fps_current = 0.0f;
static bool     st_phosphorEnabled = false;

/* ESP32-P4: render every frame (no skip needed at 360MHz) */

/* ===================================================================
 * Video Task
 * =================================================================== */
static void st_videoTask(void *arg)
{
    st_videoTaskIsRunning = true;
    while (st_videoTaskIsRunning)
    {
        uint8_t *param;
        if (xQueuePeek(st_vidQueue, &param, pdMS_TO_TICKS(100)) == pdTRUE)
        {
            /* Copy current TIA frame and grab previous */
            memcpy(st_framebuffer, param, sizeof(st_framebuffer));

            int start_line, end_line, top;
            if (st_isPal) {
                top = 0;
                start_line = (250 - DISPLAY_HEIGHT) / 2;
                end_line = start_line + DISPLAY_HEIGHT;
            } else {
                int ntsc_height = 210;  /* typical NTSC visible height */
                top = (DISPLAY_HEIGHT - ntsc_height) / 2;
                if (top < 0) top = 0;
                start_line = 0;
                end_line = ntsc_height;
                if (end_line > DISPLAY_HEIGHT) end_line = DISPLAY_HEIGHT;
            }

            /* Clear top padding */
            if (top > 0) {
                memset(st_displayBuffer, 0, DISPLAY_WIDTH * top * sizeof(uint16_t));
            }

            /* Convert visible lines: apply phosphor blending only
             * for games that set Display_Phosphor=YES (e.g. Asteroids).
             * Other games render current frame directly. */
            for (int y = start_line; y < end_line; y++) {
                int dst_y = y - start_line + top;
                if (dst_y >= DISPLAY_HEIGHT) break;
                uint16_t *dst = &st_displayBuffer[dst_y * DISPLAY_WIDTH];
                uint8_t *src_cur  = &st_framebuffer[y * STELLA_WIDTH];

                if (st_phosphorEnabled) {
                    uint8_t *src_prev = &st_prev_framebuffer[y * STELLA_WIDTH];
                    for (int x = 0; x < STELLA_WIDTH; x++) {
                        uint16_t c_cur  = st_pal16[src_cur[x]];
                        uint16_t c_prev = st_pal16[src_prev[x]];

                        /* Max-per-component blend (phosphor persistence) */
                        uint16_t r = (c_cur & 0xF800) > (c_prev & 0xF800) ? (c_cur & 0xF800) : (c_prev & 0xF800);
                        uint16_t g = (c_cur & 0x07E0) > (c_prev & 0x07E0) ? (c_cur & 0x07E0) : (c_prev & 0x07E0);
                        uint16_t b = (c_cur & 0x001F) > (c_prev & 0x001F) ? (c_cur & 0x001F) : (c_prev & 0x001F);
                        uint16_t pixel = r | g | b;

                        dst[x * 2] = pixel;
                        dst[x * 2 + 1] = pixel;
                    }
                } else {
                    for (int x = 0; x < STELLA_WIDTH; x++) {
                        uint16_t pixel = st_pal16[src_cur[x]];
                        dst[x * 2] = pixel;
                        dst[x * 2 + 1] = pixel;
                    }
                }
            }

            /* Save current frame as previous for next phosphor blend */
            if (st_phosphorEnabled) {
                memcpy(st_prev_framebuffer, st_framebuffer, sizeof(st_framebuffer));
            }

            /* Clear bottom padding */
            int used_lines = (end_line - start_line) + top;
            if (used_lines < DISPLAY_HEIGHT) {
                memset(&st_displayBuffer[used_lines * DISPLAY_WIDTH], 0,
                       (DISPLAY_HEIGHT - used_lines) * DISPLAY_WIDTH * sizeof(uint16_t));
            }

            /* Volume overlay */
            if (volume_show_frames > 0) {
                volume_show_frames--;
                /* Draw volume bar in top-right corner */
                const int bar_x = DISPLAY_WIDTH - 40;
                const int bar_y = 4;
                const int bar_w = 36;
                const int bar_h = 8;
                /* Background */
                for (int y = bar_y; y < bar_y + bar_h && y < DISPLAY_HEIGHT; y++) {
                    for (int x = bar_x; x < bar_x + bar_w && x < DISPLAY_WIDTH; x++) {
                        st_displayBuffer[y * DISPLAY_WIDTH + x] = 0x0000;
                    }
                }
                /* Filled portion */
                int fill_w = (bar_w * volume_level) / 4;
                for (int y = bar_y + 1; y < bar_y + bar_h - 1 && y < DISPLAY_HEIGHT; y++) {
                    for (int x = bar_x + 1; x < bar_x + 1 + fill_w && x < DISPLAY_WIDTH; x++) {
                        st_displayBuffer[y * DISPLAY_WIDTH + x] = 0x07E0; /* green */
                    }
                }
            }

            ili9341_write_frame_rgb565(st_displayBuffer);

            xQueueReceive(st_vidQueue, &param, portMAX_DELAY);
        }
    }
    vTaskDelete(NULL);
}

/* ===================================================================
 * 5×7 Bitmap Font for menu rendering
 * =================================================================== */
static const uint8_t st_font5x7[][7] = {
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
    /* '!' */ {0x04,0x04,0x04,0x04,0x04,0x00,0x04},
    /* '>' */ {0x10,0x08,0x04,0x02,0x04,0x08,0x10},
    /* '.' */ {0x00,0x00,0x00,0x00,0x00,0x00,0x04},
    /* 'a'-'z' lowercase */
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

static int st_font_index(char c)
{
    if (c == ' ')  return 0;
    if (c >= 'A' && c <= 'Z') return 1 + (c - 'A');
    if (c == '!')  return 27;
    if (c == '>')  return 28;
    if (c == '.')  return 29;
    if (c >= 'a' && c <= 'z') return 30 + (c - 'a');
    return 0;
}

static void st_draw_char(uint16_t *fb, int px, int py, char c, uint16_t color)
{
    int idx = st_font_index(c);
    const uint8_t *glyph = st_font5x7[idx];
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

static void st_draw_string(uint16_t *fb, int px, int py, const char *str, uint16_t color)
{
    while (*str) {
        st_draw_char(fb, px, py, *str, color);
        px += 6;
        str++;
    }
}

static void st_fill_rect(uint16_t *fb, int x, int y, int w, int h, uint16_t color)
{
    for (int row = y; row < y + h && row < DISPLAY_HEIGHT; row++) {
        if (row < 0) continue;
        for (int col = x; col < x + w && col < DISPLAY_WIDTH; col++) {
            if (col < 0) continue;
            fb[row * DISPLAY_WIDTH + col] = color;
        }
    }
}

/* ===================================================================
 * In-game menu
 * =================================================================== */
enum MenuChoice { MENU_RESUME = 0, MENU_SAVE, MENU_LOAD, MENU_EXIT };

static int show_stella_menu(void)
{
    uint16_t *menu_fb = (uint16_t *)heap_caps_malloc(
        DISPLAY_WIDTH * DISPLAY_HEIGHT * sizeof(uint16_t), MALLOC_CAP_SPIRAM);
    if (!menu_fb) return MENU_RESUME;

    const char *options[] = {"Resume Game", "Save State", "Load State", "Exit Game"};
    const int count = 4;
    int selected = 0;

    odroid_gamepad_state prev;
    odroid_input_gamepad_read(&prev);

    while (true)
    {
        /* Black background */
        st_fill_rect(menu_fb, 0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT, 0x0000);

        int box_w = 160;
        int box_h = 20 + count * 14 + 10;
        int box_x = (DISPLAY_WIDTH - box_w) / 2;
        int box_y = (DISPLAY_HEIGHT - box_h) / 2;

        /* Box with white border */
        st_fill_rect(menu_fb, box_x, box_y, box_w, box_h, 0x0000);
        st_fill_rect(menu_fb, box_x, box_y, box_w, 1, 0xFFFF);
        st_fill_rect(menu_fb, box_x, box_y + box_h - 1, box_w, 1, 0xFFFF);
        st_fill_rect(menu_fb, box_x, box_y, 1, box_h, 0xFFFF);
        st_fill_rect(menu_fb, box_x + box_w - 1, box_y, 1, box_h, 0xFFFF);

        /* Title */
        st_draw_string(menu_fb, box_x + (box_w - 9*6)/2, box_y + 5, "GAME MENU", 0xFFE0);

        /* Options */
        for (int i = 0; i < count; i++) {
            int oy = box_y + 18 + i * 14;
            int ox = box_x + 16;
            uint16_t color = (i == selected) ? 0xFFE0 : 0xFFFF;
            st_fill_rect(menu_fb, box_x + 2, oy - 1, box_w - 4, 10, 0x0000);
            if (i == selected)
                st_draw_char(menu_fb, box_x + 6, oy, '>', 0xFFE0);
            st_draw_string(menu_fb, ox, oy, options[i], color);
        }

        ili9341_write_frame_rgb565(menu_fb);

        odroid_gamepad_state state;
        odroid_input_gamepad_read(&state);

        if (!prev.values[ODROID_INPUT_DOWN] && state.values[ODROID_INPUT_DOWN])
            selected = (selected + 1) % count;
        if (!prev.values[ODROID_INPUT_UP] && state.values[ODROID_INPUT_UP])
            selected = (selected - 1 + count) % count;
        if (!prev.values[ODROID_INPUT_A] && state.values[ODROID_INPUT_A])
            break;
        if (!prev.values[ODROID_INPUT_B] && state.values[ODROID_INPUT_B]) {
            selected = MENU_RESUME;
            break;
        }

        prev = state;
        vTaskDelay(pdMS_TO_TICKS(20));
    }

    /* Wait for key release */
    odroid_gamepad_state rel;
    int timeout = 50;
    do {
        vTaskDelay(pdMS_TO_TICKS(20));
        odroid_input_gamepad_read(&rel);
        if (--timeout <= 0) break;
    } while (rel.values[ODROID_INPUT_A] || rel.values[ODROID_INPUT_B]);

    free(menu_fb);
    return selected;
}

/* ===================================================================
 * Save / Load state
 * =================================================================== */
static void ensure_save_dir(void)
{
    struct stat st;
    const char *dirs[] = { "/sd/odroid", "/sd/odroid/data", "/sd/odroid/data/a26" };
    for (int i = 0; i < 3; i++) {
        if (stat(dirs[i], &st) != 0)
            mkdir(dirs[i], 0777);
    }
}

static void build_save_path(const char *romfile)
{
    const char *name = romfile;
    const char *p = romfile;
    while (*p) {
        if (*p == '/' || *p == '\\') name = p + 1;
        p++;
    }
    snprintf(st_save_path, sizeof(st_save_path), "/sd/odroid/data/a26/%s.sav", name);
    printf("stella_run: Save path: %s\n", st_save_path);
}

static bool stella_save_state(void)
{
    if (!st_console || st_save_path[0] == '\0') return false;
    ensure_save_dir();

    string savefile(st_save_path);
    Serializer ser(savefile);
    if (!ser.isValid()) {
        printf("stella_run: ERROR cannot open %s for writing\n", st_save_path);
        return false;
    }
    ser.reset();
    if (!st_console->save(ser)) {
        printf("stella_run: ERROR Console::save failed\n");
        return false;
    }
    printf("stella_run: State saved OK to %s\n", st_save_path);
    return true;
}

static bool stella_load_state(void)
{
    if (!st_console || st_save_path[0] == '\0') return false;

    struct stat st;
    if (stat(st_save_path, &st) != 0) {
        printf("stella_run: No save file: %s\n", st_save_path);
        return false;
    }

    string savefile(st_save_path);
    Serializer ser(savefile, true);  /* readonly */
    if (!ser.isValid()) {
        printf("stella_run: ERROR cannot open %s for reading\n", st_save_path);
        return false;
    }
    ser.reset();
    if (!st_console->load(ser)) {
        printf("stella_run: ERROR Console::load failed\n");
        return false;
    }
    printf("stella_run: State loaded OK from %s\n", st_save_path);
    return true;
}

/* ===================================================================
 * Stella init
 * =================================================================== */
static bool stella_init(const char *filename)
{
    printf("stella_init: HEAP=0x%x SPIRAM=%u\n",
           (unsigned)esp_get_free_heap_size(),
           (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));

    FILE *fp = fopen(filename, "rb");
    if (!fp) {
        printf("stella_init: ERROR - cannot open '%s'\n", filename);
        return false;
    }

    fseek(fp, 0, SEEK_END);
    size_t size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    printf("stella_init: ROM size = %u bytes\n", (unsigned)size);

    void *data = heap_caps_malloc(size, MALLOC_CAP_SPIRAM);
    if (!data) {
        printf("stella_init: ERROR - cannot allocate %u bytes\n", (unsigned)size);
        fclose(fp);
        return false;
    }

    size_t count = fread(data, 1, size, fp);
    fclose(fp);
    if (count != size) {
        printf("stella_init: ERROR - read %u of %u bytes\n", (unsigned)count, (unsigned)size);
        free(data);
        return false;
    }

    /* Compute MD5 */
    string cartMD5 = MD5((uInt8 *)data, (uInt32)size);
    printf("stella_init: MD5 = %s\n", cartMD5.c_str());

    st_osystem = new OSystem();
    Properties props;
    st_osystem->propSet().getMD5(cartMD5, props);

    string cartType = props.get(Cartridge_Type);
    string cartId;

    st_settings = new Settings(st_osystem);
    st_settings->setValue("romloadcount", false);

    printf("stella_init: Creating cartridge (type='%s')...\n", cartType.c_str());
    st_cartridge = Cartridge::create(
        (const uInt8 *)data, (uInt32)size, cartMD5,
        cartType, cartId, *st_osystem, *st_settings);

    if (!st_cartridge) {
        printf("stella_init: Failed to load cartridge.\n");
        free(data);
        return false;
    }

    /* Create console */
    printf("stella_init: Creating console...\n");
    st_console = new Console(st_osystem, st_cartridge, props);

    if (!st_console) {
        printf("stella_init: Failed to create console.\n");
        free(data);
        return false;
    }
    st_osystem->myConsole = st_console;

    /* Init video & audio */
    st_console->initializeVideo();
    st_console->initializeAudio();

    TIA &tia = st_console->tia();
    int videoWidth = tia.width();
    int videoHeight = tia.height();
    st_isPal = (videoHeight > 210);
    printf("stella_init: videoWidth=%d videoHeight=%d isPal=%d\n",
           videoWidth, videoHeight, st_isPal ? 1 : 0);

    /* Build palette (RGB565, little-endian for PPA) */
    const uint32_t *palette = st_console->getPalette(0);
    for (int i = 0; i < 256; i++) {
        uint32_t c = palette[i];
        uint16_t r = (c >> 16) & 0xFF;
        uint16_t g = (c >> 8) & 0xFF;
        uint16_t b = c & 0xFF;
        st_pal16[i] = ((r << 8) & 0xF800) | ((g << 3) & 0x07E0) | (b >> 3);
    }

    /* Check if this game uses phosphor blending */
    st_phosphorEnabled = (props.get(Display_Phosphor) == "YES");
    printf("stella_init: phosphor=%s\n", st_phosphorEnabled ? "YES" : "NO");

    st_tiaSamplesPerFrame = (uint32_t)(31400.0f / st_console->getFramerate());
    printf("stella_init: tiaSamplesPerFrame=%u\n", (unsigned)st_tiaSamplesPerFrame);

    /* Free ROM data — cart has its own copy */
    free(data);

    return true;
}

/* ===================================================================
 * Stella step (one frame)
 * =================================================================== */
static void stella_step(odroid_gamepad_state *gamepad)
{
    Event &ev = st_osystem->eventHandler().event();

    ev.set(Event::Type(Event::JoystickZeroUp),    gamepad->values[ODROID_INPUT_UP]);
    ev.set(Event::Type(Event::JoystickZeroDown),   gamepad->values[ODROID_INPUT_DOWN]);
    ev.set(Event::Type(Event::JoystickZeroLeft),   gamepad->values[ODROID_INPUT_LEFT]);
    ev.set(Event::Type(Event::JoystickZeroRight),  gamepad->values[ODROID_INPUT_RIGHT]);
    ev.set(Event::Type(Event::JoystickZeroFire),   gamepad->values[ODROID_INPUT_A]);
    ev.set(Event::Type(Event::ConsoleSelect),      gamepad->values[ODROID_INPUT_SELECT]);
    ev.set(Event::Type(Event::ConsoleReset),       gamepad->values[ODROID_INPUT_START]);

    /* Analog paddle: ADC → Stelladaptor axis (-32767..+32767)
       The Paddles controller reads SALeftAxis0Value to set resistance.
       Direct mapping: ADC_LO → -32767 (left), ADC_HI → +32767 (right). */
    if (st_paddle_adc_enabled &&
        st_console->controller(Controller::Left).type() == Controller::Paddles)
    {
        static int ema_adc = -1;
        static int last_axis = -99999;
        int raw_adc = odroid_paddle_adc_raw;

        if (raw_adc >= 0) {
            /* EMA smoothing: alpha ~0.2 (51/256) */
            if (ema_adc < 0)
                ema_adc = raw_adc;
            else
                ema_adc = (51 * raw_adc + 205 * ema_adc + 128) >> 8;

            int clamped = ema_adc;
            if (clamped < PADDLE_ADC_LO) clamped = PADDLE_ADC_LO;
            if (clamped > PADDLE_ADC_HI) clamped = PADDLE_ADC_HI;

            /* Direct: low ADC = low axis (left), high ADC = high axis (right) */
            int paddle_axis = (int)((long)(clamped - PADDLE_ADC_LO) * 65534
                                    / (PADDLE_ADC_HI - PADDLE_ADC_LO)) - 32767;

            int diff = paddle_axis - last_axis;
            if (diff < 0) diff = -diff;
            if (last_axis == -99999 || diff > 200) {
                last_axis = paddle_axis;
                ev.set(Event::Type(Event::SALeftAxis0Value), paddle_axis);
            }
        }

        /* Paddle fire = A button */
        ev.set(Event::Type(Event::PaddleZeroFire), gamepad->values[ODROID_INPUT_A]);
    }

    st_console->controller(Controller::Left).update();
    st_console->controller(Controller::Right).update();
    st_console->switches().update();

    /* Emulate one frame */
    TIA &tia = st_console->tia();
    tia.update();

    /* Audio */
    if (!st_sampleBuffer) {
        st_sampleBuffer = (int32_t *)malloc(st_tiaSamplesPerFrame * sizeof(int32_t));
        if (!st_sampleBuffer) {
            printf("stella_step: ERROR allocating sample buffer\n");
            return;
        }
    }

    SoundSDL *sound = (SoundSDL *)&st_osystem->sound();
    sound->processFragment((int16_t *)st_sampleBuffer, st_tiaSamplesPerFrame);
    odroid_audio_submit((int16_t *)st_sampleBuffer, st_tiaSamplesPerFrame);
}

/* ===================================================================
 * Cleanup
 * =================================================================== */
static void stella_cleanup(void)
{
    st_videoTaskIsRunning = false;
    if (st_videoTaskHandle) {
        vTaskDelay(pdMS_TO_TICKS(200));
        st_videoTaskHandle = NULL;
    }
    if (st_vidQueue) {
        vQueueDelete(st_vidQueue);
        st_vidQueue = NULL;
    }

    if (st_sampleBuffer) { free(st_sampleBuffer); st_sampleBuffer = NULL; }
    if (st_displayBuffer) { free(st_displayBuffer); st_displayBuffer = NULL; }

    /* Stella objects */
    if (st_console)   { delete st_console;   st_console = NULL; }
    if (st_settings)  { delete st_settings;  st_settings = NULL; }
    if (st_osystem)   { delete st_osystem;   st_osystem = NULL; }
    st_cartridge = NULL; /* owned by console */
}

/* ===================================================================
 * Main entry point
 * =================================================================== */
extern "C" void stella_run(const char *rom_path)
{
    printf("stella_run: Starting Atari 2600 emulator\n");
    printf("stella_run: ROM = %s\n", rom_path);
    printf("stella_run: Free heap = %u, SPIRAM = %u\n",
           (unsigned)esp_get_free_heap_size(),
           (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));

    st_exitRequested = false;
    st_menu_request = false;

    /* Allocate display buffer in SPIRAM */
    st_displayBuffer = (uint16_t *)heap_caps_malloc(
        DISPLAY_WIDTH * DISPLAY_HEIGHT * sizeof(uint16_t), MALLOC_CAP_SPIRAM);
    if (!st_displayBuffer) {
        printf("stella_run: ERROR allocating display buffer\n");
        return;
    }
    memset(st_displayBuffer, 0, DISPLAY_WIDTH * DISPLAY_HEIGHT * sizeof(uint16_t));
    memset(st_prev_framebuffer, 0, sizeof(st_prev_framebuffer));

    /* Initialize emulator */
    if (!stella_init(rom_path)) {
        printf("stella_run: Initialization failed!\n");
        if (st_displayBuffer) { free(st_displayBuffer); st_displayBuffer = NULL; }
        return;
    }

    /* Build save path */
    build_save_path(rom_path);

    /* Check for resume */
    int startAction = odroid_settings_StartAction_get();
    printf("stella_run: StartAction=%d\n", startAction);
    if (startAction == ODROID_START_ACTION_RESTART && st_save_path[0] != '\0') {
        if (stella_load_state()) {
            printf("stella_run: Resumed from saved state\n");
        }
    }
    odroid_settings_StartAction_set(ODROID_START_ACTION_NORMAL);

    /* Init audio */
    odroid_audio_init(AUDIO_SAMPLE_RATE);

    /* ─── Paddle pot detection on GPIO 52 (ADC2_CH3) ─────────── */
    {
        odroid_paddle_adc_init();
        int lo = 4095, hi = 0;
        for (int i = 0; i < 4; i++) {
            odroid_gamepad_state dummy;
            odroid_input_gamepad_read(&dummy);
            int v = odroid_paddle_adc_raw;
            if (v < lo) lo = v;
            if (v > hi) hi = v;
            vTaskDelay(pdMS_TO_TICKS(5));
        }
        int spread = hi - lo;
        if (spread < PADDLE_DETECT_SPREAD) {
            st_paddle_adc_enabled = true;
            printf("Stella: Paddle pot DETECTED (spread=%d, lo=%d hi=%d)\n", spread, lo, hi);
        } else {
            st_paddle_adc_enabled = false;
            printf("Stella: Paddle pot NOT detected (spread=%d), no analog paddle\n", spread);
        }
    }

    /* Create video task */
    st_vidQueue = xQueueCreate(1, sizeof(uint8_t *));
    xTaskCreatePinnedToCore(&st_videoTask, "st_vidTask", 1024 * 4, NULL, 5,
                            &st_videoTaskHandle, 1);

    /* Main emulation loop */
    odroid_gamepad_state last_gamepad;
    odroid_input_gamepad_read(&last_gamepad);

    int frame = 0;
    int renderFrames = 0;
    int64_t totalElapsedTime = 0;

    fps_last_time = esp_timer_get_time();

    while (!st_exitRequested)
    {
        int64_t startTime = esp_timer_get_time();

        odroid_gamepad_state gamepad;
        odroid_input_gamepad_read(&gamepad);

        /* Menu button (X on USB gamepad) */
        if (last_gamepad.values[ODROID_INPUT_MENU] &&
            !gamepad.values[ODROID_INPUT_MENU])
        {
            /* Drain video queue */
            uint8_t *dummy;
            while (xQueueReceive(st_vidQueue, &dummy, 0) == pdTRUE) {}
            vTaskDelay(pdMS_TO_TICKS(50));

            int choice = show_stella_menu();
            switch (choice) {
            case MENU_SAVE:
                stella_save_state();
                break;
            case MENU_LOAD:
                stella_load_state();
                break;
            case MENU_EXIT:
                st_exitRequested = true;
                break;
            default: /* RESUME */
                break;
            }

            odroid_input_gamepad_read(&last_gamepad);
            if (st_exitRequested) break;
            continue;
        }

        /* Volume button (Y on USB gamepad) */
        if (!last_gamepad.values[ODROID_INPUT_VOLUME] &&
            gamepad.values[ODROID_INPUT_VOLUME])
        {
            odroid_audio_volume_change();
            volume_level = odroid_audio_volume_get();
            volume_show_frames = 90;
            printf("stella_run: Volume=%d\n", volume_level);
        }

        stella_step(&gamepad);

        /* ESP32-P4: render every frame */
        {
            TIA &tia = st_console->tia();
            uint8_t *fb = tia.currentFrameBuffer();
            xQueueSend(st_vidQueue, &fb, portMAX_DELAY);
            renderFrames++;
        }

        last_gamepad = gamepad;

        /* FPS counter */
        int64_t stopTime = esp_timer_get_time();
        totalElapsedTime += (stopTime - startTime);
        frame++;

        if (frame >= 60) {
            float seconds = totalElapsedTime / 1000000.0f;
            fps_current = frame / seconds;
            float renderFps = renderFrames / seconds;
            printf("stella_run: SIM:%.1f REN:%.1f HEAP:%u SPIRAM:%u\n",
                   fps_current, renderFps,
                   (unsigned)esp_get_free_heap_size(),
                   (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
            frame = 0;
            renderFrames = 0;
            totalElapsedTime = 0;
        }
    }

    /* Cleanup */
    printf("stella_run: Exiting emulator\n");
    stella_cleanup();
    printf("stella_run: Cleanup complete\n");
}
