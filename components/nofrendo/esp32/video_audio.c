// Copyright 2015-2016 Espressif Systems (Shanghai) PTE LTD
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at

//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <freertos/FreeRTOS.h>
#include <freertos/timers.h>
#include <freertos/task.h>
#include <freertos/queue.h>

//Nes stuff wants to define this as well...
#undef false
#undef true
#undef bool


#include <math.h>
#include <string.h>
#include <noftypes.h>
#include <bitmap.h>
#include <nofconfig.h>
#include <event.h>
#include <gui.h>
#include <log.h>
#include <nes.h>
#include <nes_pal.h>
#include <nesinput.h>
#include <osd.h>
#include <stdint.h>
#include "sdkconfig.h"
#include "nesstate.h"

#include "hourglass_empty_black_48dp.h"
#include "odroid_settings.h"
#include "odroid_audio.h"
#include "odroid_system.h"
#include "odroid_sdcard.h"
#include "odroid_display.h"
#include "odroid_input.h"
#include "esp_heap_caps.h"

#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>


#define DEFAULT_SAMPLERATE   32000
#define  DEFAULT_FRAGSIZE     512

#define  DEFAULT_WIDTH        256
#define  DEFAULT_HEIGHT       NES_VISIBLE_HEIGHT

odroid_volume_level Volume;
odroid_battery_state battery;
int scaling_enabled = 1;
int previous_scaling_enabled = 1;

// Quit flag — set by menu/power handler, checked by main emulation loop
volatile bool nofrendo_quit_flag = false;

//Seemingly, this will be called only once. Should call func with a freq of frequency,
int osd_installtimer(int frequency, void *func, int funcsize, void *counter, int countersize)
{
   return 0;
}


/*
** Audio
*/
static void (*audio_callback)(void *buffer, int length) = NULL;
#if CONFIG_SOUND_ENA
		QueueHandle_t queue;
		static int16_t *audio_frame;
#endif

void do_audio_frame() {
#if CONFIG_SOUND_ENA
		int remaining = DEFAULT_SAMPLERATE / NES_REFRESH_RATE;
		while(remaining)
		{
			int n=DEFAULT_FRAGSIZE;
			if (n>remaining) n=remaining;

			audio_callback(audio_frame, n); //get more data

			//16 bit mono -> 32-bit (16 bit r+l)
			for (int i=n-1; i>=0; i--)
			{
				int sample = (int)audio_frame[i];

				audio_frame[i*2]= (short)sample;
                audio_frame[i*2+1] = (short)sample;
			}

            odroid_audio_submit(audio_frame, n);

			remaining -= n;
		}
#endif
}

void osd_setsound(void (*playfunc)(void *buffer, int length))
{
   //Indicates we should call playfunc() to get more data.
   audio_callback = playfunc;
}

static void osd_stopsound(void)
{
   audio_callback = NULL;
}


static int osd_init_sound(void)
{
#if CONFIG_SOUND_ENA

	if (!audio_frame)
		audio_frame = malloc(4 * DEFAULT_FRAGSIZE);

    odroid_audio_init(DEFAULT_SAMPLERATE);

#endif

	audio_callback = NULL;

	return 0;
}

void osd_getsoundinfo(sndinfo_t *info)
{
   info->sample_rate = DEFAULT_SAMPLERATE;
   info->bps = 16;
}

/*
** Video
*/

static int init(int width, int height);
static void shutdown(void);
static int set_mode(int width, int height);
static void set_palette(rgb_t *pal);
static void clear(uint8 color);
static bitmap_t *lock_write(void);
static void free_write(int num_dirties, rect_t *dirty_rects);
static void custom_blit(bitmap_t *bmp, int num_dirties, rect_t *dirty_rects);
static char fb[1]; //dummy

QueueHandle_t vidQueue;

viddriver_t sdlDriver =
{
   "Simple DirectMedia Layer",         /* name */
   init,          /* init */
   shutdown,      /* shutdown */
   set_mode,      /* set_mode */
   set_palette,   /* set_palette */
   clear,         /* clear */
   lock_write,    /* lock_write */
   free_write,    /* free_write */
   custom_blit,   /* custom_blit */
   false          /* invalidate flag */
};


bitmap_t *myBitmap;

void osd_getvideoinfo(vidinfo_t *info)
{
   info->default_width = DEFAULT_WIDTH;
   info->default_height = DEFAULT_HEIGHT;
   info->driver = &sdlDriver;
}

/* flip between full screen and windowed */
void osd_togglefullscreen(int code)
{
}

/* initialise video */
static int init(int width, int height)
{
	return 0;
}

static void shutdown(void)
{
}

/* set a video mode */
static int set_mode(int width, int height)
{
   return 0;
}

uint16 myPalette[256];

/* copy nes palette over to hardware */
static void set_palette(rgb_t *pal)
{
	uint16 c;

   int i;

   for (i = 0; i < 256; i++)
   {
      c=(pal[i].b>>3)+((pal[i].g>>2)<<5)+((pal[i].r>>3)<<11);
      myPalette[i]=(c>>8)|((c&0xff)<<8);
      //myPalette[i]= c;
   }

}

/* clear all frames to a particular color */
static void clear(uint8 color)
{
//   SDL_FillRect(mySurface, 0, color);
}



/* acquire the directbuffer for writing */
static bitmap_t *lock_write(void)
{
//   SDL_LockSurface(mySurface);
   myBitmap = bmp_createhw((uint8*)fb, DEFAULT_WIDTH, DEFAULT_HEIGHT, DEFAULT_WIDTH*2);
   return myBitmap;
}

/* release the resource */
static void free_write(int num_dirties, rect_t *dirty_rects)
{
   bmp_destroy(&myBitmap);
}

static uint8_t *lcdfb[2] = {NULL, NULL};
static int lcdfb_write_idx = 0;

static void custom_blit(bitmap_t *bmp, int num_dirties, rect_t *dirty_rects) {
    if (bmp->line[0] != NULL && lcdfb[0] != NULL)
    {
        /* Write to the alternate buffer so videoTask can safely read the other */
        int next = lcdfb_write_idx ^ 1;
        memcpy(lcdfb[next], bmp->line[0], 256 * 224);
        lcdfb_write_idx = next;

        void* arg = (void*)lcdfb[next];
        /* Non-blocking overwrite: drop frame if display is still busy */
        xQueueOverwrite(vidQueue, &arg);
    }
}


//This runs on core 1.
volatile bool exitVideoTaskFlag = false;
static void videoTask(void *arg) {
    uint8_t* bmp = NULL;

    while(1)
	{
		xQueuePeek(vidQueue, &bmp, portMAX_DELAY);

        if (bmp == 1) break;

        if (previous_scaling_enabled != scaling_enabled)
        {
            // Clear display
            ili9341_write_frame_nes(NULL, NULL, true);
            previous_scaling_enabled = scaling_enabled;
        }

        ili9341_write_frame_nes(bmp, myPalette, scaling_enabled);

        odroid_input_battery_level_read(&battery);

		xQueueReceive(vidQueue, &bmp, portMAX_DELAY);
	}


    odroid_display_lock_nes_display();

    odroid_display_show_hourglass();

    odroid_display_unlock_nes_display();
    //odroid_display_drain_spi();

    exitVideoTaskFlag = true;

    vTaskDelete(NULL);

    while(1){}
}


/*
** Input
*/

static void osd_initinput()
{
}

/* ─── Minimal 5×7 bitmap font for in-game menu ─────────────────── */
/* Each character is 5 pixels wide, 7 pixels tall.
   Stored as 7 bytes per char, each byte has bits [4..0] = pixels. */
static const uint8_t font5x7[][7] = {
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
    /* 'a' */ {0x00,0x00,0x0E,0x01,0x0F,0x11,0x0F},
    /* 'b' */ {0x10,0x10,0x1E,0x11,0x11,0x11,0x1E},
    /* 'c' */ {0x00,0x00,0x0E,0x11,0x10,0x11,0x0E},
    /* 'd' */ {0x01,0x01,0x0F,0x11,0x11,0x11,0x0F},
    /* 'e' */ {0x00,0x00,0x0E,0x11,0x1F,0x10,0x0E},
    /* 'f' */ {0x06,0x08,0x08,0x1C,0x08,0x08,0x08},
    /* 'g' */ {0x00,0x00,0x0F,0x11,0x0F,0x01,0x0E},
    /* 'h' */ {0x10,0x10,0x1E,0x11,0x11,0x11,0x11},
    /* 'i' */ {0x04,0x00,0x0C,0x04,0x04,0x04,0x0E},
    /* 'j' */ {0x02,0x00,0x06,0x02,0x02,0x12,0x0C},
    /* 'k' */ {0x10,0x10,0x12,0x14,0x18,0x14,0x12},
    /* 'l' */ {0x0C,0x04,0x04,0x04,0x04,0x04,0x0E},
    /* 'm' */ {0x00,0x00,0x1A,0x15,0x15,0x15,0x15},
    /* 'n' */ {0x00,0x00,0x1E,0x11,0x11,0x11,0x11},
    /* 'o' */ {0x00,0x00,0x0E,0x11,0x11,0x11,0x0E},
    /* 'p' */ {0x00,0x00,0x1E,0x11,0x1E,0x10,0x10},
    /* 'q' */ {0x00,0x00,0x0F,0x11,0x0F,0x01,0x01},
    /* 'r' */ {0x00,0x00,0x16,0x19,0x10,0x10,0x10},
    /* 's' */ {0x00,0x00,0x0F,0x10,0x0E,0x01,0x1E},
    /* 't' */ {0x08,0x08,0x1C,0x08,0x08,0x09,0x06},
    /* 'u' */ {0x00,0x00,0x11,0x11,0x11,0x11,0x0F},
    /* 'v' */ {0x00,0x00,0x11,0x11,0x11,0x0A,0x04},
    /* 'w' */ {0x00,0x00,0x11,0x11,0x15,0x15,0x0A},
    /* 'x' */ {0x00,0x00,0x11,0x0A,0x04,0x0A,0x11},
    /* 'y' */ {0x00,0x00,0x11,0x11,0x0F,0x01,0x0E},
    /* 'z' */ {0x00,0x00,0x1F,0x02,0x04,0x08,0x1F},
};

static int menu_font_index(char c)
{
    if (c == ' ')  return 0;
    if (c >= 'A' && c <= 'Z') return 1 + (c - 'A');
    if (c == '!')  return 27;
    if (c == '>')  return 28;
    if (c == '.')  return 29;
    if (c >= 'a' && c <= 'z') return 30 + (c - 'a');
    return 0; /* default = space */
}

/* Draw a single character at (px,py) on the 320×240 framebuffer.
   color is byte-swapped RGB565 (LE for our framebuffer). */
static void menu_draw_char(uint16_t *fb, int px, int py, char c, uint16_t color)
{
    int idx = menu_font_index(c);
    const uint8_t *glyph = font5x7[idx];
    for (int row = 0; row < 7; row++) {
        int yy = py + row;
        if (yy < 0 || yy >= 240) continue;
        uint8_t bits = glyph[row];
        for (int col = 0; col < 5; col++) {
            int xx = px + col;
            if (xx < 0 || xx >= 320) continue;
            if (bits & (0x10 >> col)) {
                fb[yy * 320 + xx] = color;
            }
        }
    }
}

/* Draw a string at (px,py). Each char is 6px wide (5 + 1 gap). */
static void menu_draw_string(uint16_t *fb, int px, int py, const char *str, uint16_t color)
{
    while (*str) {
        menu_draw_char(fb, px, py, *str, color);
        px += 6;
        str++;
    }
}

/* Fill a rectangle on the 320×240 framebuffer */
static void menu_fill_rect(uint16_t *fb, int x, int y, int w, int h, uint16_t color)
{
    for (int row = y; row < y + h && row < 240; row++) {
        if (row < 0) continue;
        for (int col = x; col < x + w && col < 320; col++) {
            if (col < 0) continue;
            fb[row * 320 + col] = color;
        }
    }
}

/* ─── Check if NES save file exists on SD ──────────────────────── */
extern const char* SD_BASE_PATH;

static bool menu_check_save_exists(void)
{
    char *romPath = odroid_settings_RomFilePath_get();
    if (!romPath) return false;

    char *fileName = odroid_util_GetFileName(romPath);
    if (!fileName) { free(romPath); return false; }

    /* Mount SD temporarily to check for save file */
    esp_err_t r = odroid_sdcard_open(SD_BASE_PATH);
    if (r != ESP_OK) { free(fileName); free(romPath); return false; }

    /* Check: /sd/odroid/data/nes/<fileName>.sav */
    char pathBuf[512];
    snprintf(pathBuf, sizeof(pathBuf), "%s/odroid/data/nes/%s.sav", SD_BASE_PATH, fileName);

    struct stat st;
    bool exists = (stat(pathBuf, &st) == 0);

    odroid_sdcard_close();
    free(fileName);
    free(romPath);
    return exists;
}

/* ─── Delete the NES save file from SD ─────────────────────────── */
static bool menu_delete_save(void)
{
    char *romPath = odroid_settings_RomFilePath_get();
    if (!romPath) return false;

    char *fileName = odroid_util_GetFileName(romPath);
    if (!fileName) { free(romPath); return false; }

    /* Mount SD temporarily to delete save file */
    esp_err_t r = odroid_sdcard_open(SD_BASE_PATH);
    if (r != ESP_OK) { free(fileName); free(romPath); return false; }

    /* Delete: /sd/odroid/data/nes/<fileName>.sav */
    char pathBuf[512];
    snprintf(pathBuf, sizeof(pathBuf), "%s/odroid/data/nes/%s.sav", SD_BASE_PATH, fileName);

    bool ok = false;
    struct stat st;
    if (stat(pathBuf, &st) == 0) {
        ok = (unlink(pathBuf) == 0);
    }

    odroid_sdcard_close();
    free(fileName);
    free(romPath);
    return ok;
}

/* ─── In-Game Menu ─────────────────────────────────────────────── */
/* Menu option IDs */
#define MENU_RESUME     0
#define MENU_RESTART    1
#define MENU_SAVE       2
#define MENU_RELOAD     3
#define MENU_OVERWRITE  4
#define MENU_DELETE     5
#define MENU_EXIT       6

/* RGB565 byte-swapped colors (little-endian for our framebuffer) */
#define COLOR_BLACK     0x0000
#define COLOR_WHITE     0xFFFF
#define COLOR_YELLOW    0xE0FF  /* 0xFFE0 byte-swapped */
#define COLOR_DKGRAY    0x2108  /* ~dark gray */
#define COLOR_GREEN     0xE007  /* 0x07E0 byte-swapped */
#define COLOR_RED       0x00F8  /* 0xF800 byte-swapped */
#define COLOR_CYAN      0xFF07  /* 0x07FF byte-swapped */

/* ─── Volume Overlay ───────────────────────────────────────────── */
/* Shows a small volume bar overlay. User can press LEFT/RIGHT to
   adjust, Y to cycle, or A/B to dismiss. Auto-dismisses after
   ~2 seconds of no input. */
static void show_volume_overlay(void)
{
    uint16_t *fb = display_get_emu_buffer();
    if (!fb) return;

    static const char *level_names[ODROID_VOLUME_LEVEL_COUNT] = {
        "MUTE", "25%", "50%", "75%", "100%"
    };

    int level = (int)odroid_audio_volume_get();
    int timeout = 25;  /* ~2 seconds at 80ms per frame */

    odroid_gamepad_state prev;
    odroid_input_gamepad_read(&prev);

    /* Debounce: wait for volume button release first */
    for (int i = 0; i < 100; i++) {
        odroid_gamepad_state tmp;
        odroid_input_gamepad_read(&tmp);
        if (!tmp.values[ODROID_INPUT_VOLUME]) break;
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    odroid_input_gamepad_read(&prev);

    while (timeout > 0) {
        odroid_display_lock_nes_display();

        /* Overlay dimensions */
        int box_w = 140;
        int box_h = 34;
        int box_x = (320 - box_w) / 2;
        int box_y = 8;  /* near top of screen */

        /* Dark background + border */
        menu_fill_rect(fb, box_x, box_y, box_w, box_h, COLOR_BLACK);
        menu_fill_rect(fb, box_x, box_y, box_w, 1, COLOR_WHITE);
        menu_fill_rect(fb, box_x, box_y + box_h - 1, box_w, 1, COLOR_WHITE);
        menu_fill_rect(fb, box_x, box_y, 1, box_h, COLOR_WHITE);
        menu_fill_rect(fb, box_x + box_w - 1, box_y, 1, box_h, COLOR_WHITE);

        /* Title: "VOLUME" + level name */
        char title[32];
        snprintf(title, sizeof(title), "VOLUME: %s", level_names[level]);
        int title_w = strlen(title) * 6;
        menu_draw_string(fb, box_x + (box_w - title_w) / 2, box_y + 4, title, COLOR_YELLOW);

        /* Volume bar background */
        int bar_x = box_x + 10;
        int bar_y = box_y + 16;
        int bar_w = box_w - 20;
        int bar_h = 10;
        menu_fill_rect(fb, bar_x, bar_y, bar_w, bar_h, COLOR_DKGRAY);

        /* Volume bar filled portion */
        if (level > 0) {
            int fill_w = (bar_w * level) / (ODROID_VOLUME_LEVEL_COUNT - 1);
            uint16_t bar_color = (level <= 1) ? COLOR_GREEN :
                                 (level <= 3) ? COLOR_CYAN : COLOR_YELLOW;
            menu_fill_rect(fb, bar_x, bar_y, fill_w, bar_h, bar_color);
        }

        /* Bar border */
        menu_fill_rect(fb, bar_x, bar_y, bar_w, 1, COLOR_WHITE);
        menu_fill_rect(fb, bar_x, bar_y + bar_h - 1, bar_w, 1, COLOR_WHITE);
        menu_fill_rect(fb, bar_x, bar_y, 1, bar_h, COLOR_WHITE);
        menu_fill_rect(fb, bar_x + bar_w - 1, bar_y, 1, bar_h, COLOR_WHITE);

        /* Segment markers */
        for (int i = 1; i < ODROID_VOLUME_LEVEL_COUNT - 1; i++) {
            int sx = bar_x + (bar_w * i) / (ODROID_VOLUME_LEVEL_COUNT - 1);
            menu_fill_rect(fb, sx, bar_y, 1, bar_h, COLOR_WHITE);
        }

        display_emu_flush();
        odroid_display_unlock_nes_display();

        /* Read input */
        vTaskDelay(pdMS_TO_TICKS(80));
        odroid_gamepad_state state;
        odroid_input_gamepad_read(&state);

        bool changed = false;

        /* LEFT = decrease volume */
        if (state.values[ODROID_INPUT_LEFT] && !prev.values[ODROID_INPUT_LEFT]) {
            if (level > 0) { level--; changed = true; }
            timeout = 25;
        }
        /* RIGHT = increase volume */
        if (state.values[ODROID_INPUT_RIGHT] && !prev.values[ODROID_INPUT_RIGHT]) {
            if (level < ODROID_VOLUME_LEVEL_COUNT - 1) { level++; changed = true; }
            timeout = 25;
        }
        /* Y (volume) = cycle up */
        if (state.values[ODROID_INPUT_VOLUME] && !prev.values[ODROID_INPUT_VOLUME]) {
            level = (level + 1) % ODROID_VOLUME_LEVEL_COUNT;
            changed = true;
            timeout = 25;
        }
        /* A or B = dismiss immediately */
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

/* Show the in-game menu. Returns true if emulation should continue,
   false if DoQuit was called and we should exit. */
static bool show_ingame_menu(void)
{
    uint16_t *fb = display_get_emu_buffer();
    if (!fb) return true;

    bool has_save = menu_check_save_exists();

    /* Build option list dynamically */
    const char *labels[8];
    int ids[8];
    int count = 0;

    labels[count] = "Resume Game";      ids[count] = MENU_RESUME;    count++;
    labels[count] = "Restart Game";     ids[count] = MENU_RESTART;   count++;
    labels[count] = "Save Game";        ids[count] = MENU_SAVE;      count++;
    if (has_save) {
        labels[count] = "Reload Game";  ids[count] = MENU_RELOAD;    count++;
        labels[count] = "Overwrite Save"; ids[count] = MENU_OVERWRITE; count++;
        labels[count] = "Delete Save";  ids[count] = MENU_DELETE;    count++;
    }
    labels[count] = "Exit Game";        ids[count] = MENU_EXIT;      count++;

    int selected = 0;
    odroid_gamepad_state prev;
    odroid_input_gamepad_read(&prev);

    /* Debounce: wait for MENU button release before entering loop */
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
        /* Lock display to prevent race with videoTask on Core 1 */
        odroid_display_lock_nes_display();

        /* ── Draw menu overlay ── */
        int box_w = 160;
        int box_h = 20 + count * 14 + 10;
        int box_x = (320 - box_w) / 2;
        int box_y = (240 - box_h) / 2;

        /* Dark background box with border */
        menu_fill_rect(fb, box_x, box_y, box_w, box_h, COLOR_BLACK);
        menu_fill_rect(fb, box_x, box_y, box_w, 1, COLOR_WHITE);
        menu_fill_rect(fb, box_x, box_y + box_h - 1, box_w, 1, COLOR_WHITE);
        menu_fill_rect(fb, box_x, box_y, 1, box_h, COLOR_WHITE);
        menu_fill_rect(fb, box_x + box_w - 1, box_y, 1, box_h, COLOR_WHITE);

        /* Title */
        menu_draw_string(fb, box_x + (box_w - 9*6)/2, box_y + 5, "GAME MENU", COLOR_YELLOW);

        /* Options */
        for (int i = 0; i < count; i++) {
            int oy = box_y + 18 + i * 14;
            int ox = box_x + 16;
            uint16_t color = (i == selected) ? COLOR_YELLOW : COLOR_WHITE;

            /* Clear option line background */
            menu_fill_rect(fb, box_x + 2, oy - 1, box_w - 4, 10, COLOR_BLACK);

            /* Cursor */
            if (i == selected) {
                menu_draw_char(fb, box_x + 6, oy, '>', COLOR_YELLOW);
            }
            menu_draw_string(fb, ox, oy, labels[i], color);
        }

        /* Flash message (e.g. "Saved!" / "Deleted!") */
        if (flash_msg && flash_timer > 0) {
            int fw = strlen(flash_msg) * 6;
            int fx = box_x + (box_w - fw) / 2;
            int fy = box_y + box_h - 12;
            menu_fill_rect(fb, box_x + 2, fy - 2, box_w - 4, 12, COLOR_BLACK);
            menu_draw_string(fb, fx, fy, flash_msg, COLOR_GREEN);
            flash_timer--;
            if (flash_timer == 0) flash_msg = NULL;
        }

        display_emu_flush();
        odroid_display_unlock_nes_display();

        /* ── Read input ── */
        vTaskDelay(pdMS_TO_TICKS(80));
        odroid_gamepad_state state;
        odroid_input_gamepad_read(&state);

        /* Navigate up */
        if (state.values[ODROID_INPUT_UP] && !prev.values[ODROID_INPUT_UP]) {
            selected = (selected - 1 + count) % count;
        }
        /* Navigate down */
        if (state.values[ODROID_INPUT_DOWN] && !prev.values[ODROID_INPUT_DOWN]) {
            selected = (selected + 1) % count;
        }

        /* B or MENU = resume */
        if ((state.values[ODROID_INPUT_B] && !prev.values[ODROID_INPUT_B]) ||
            (state.values[ODROID_INPUT_MENU] && !prev.values[ODROID_INPUT_MENU])) {
            menu_active = false;
            keep_running = true;
        }

        /* A = select option */
        if (state.values[ODROID_INPUT_A] && !prev.values[ODROID_INPUT_A]) {
            switch (ids[selected]) {
                case MENU_RESUME:
                    menu_active = false;
                    keep_running = true;
                    break;

                case MENU_RESTART:
                    printf("InGameMenu: Restart Game (hard reset)\n");
                    nes_reset(HARD_RESET);
                    menu_active = false;
                    keep_running = true;
                    break;

                case MENU_RELOAD:
                    printf("InGameMenu: Reload Game (load save state)\n");
                    load_sram();
                    flash_msg = "Loaded!";
                    flash_timer = 15;
                    menu_active = false;
                    keep_running = true;
                    break;

                case MENU_SAVE:
                    printf("InGameMenu: Save Game\n");
                    save_sram();
                    if (!has_save) {
                        has_save = true;
                        /* Rebuild menu with save-dependent options */
                        count = 0;
                        labels[count] = "Resume Game";      ids[count] = MENU_RESUME;    count++;
                        labels[count] = "Restart Game";     ids[count] = MENU_RESTART;   count++;
                        labels[count] = "Save Game";        ids[count] = MENU_SAVE;      count++;
                        labels[count] = "Reload Game";      ids[count] = MENU_RELOAD;    count++;
                        labels[count] = "Overwrite Save";   ids[count] = MENU_OVERWRITE; count++;
                        labels[count] = "Delete Save";      ids[count] = MENU_DELETE;    count++;
                        labels[count] = "Exit Game";        ids[count] = MENU_EXIT;      count++;
                    }
                    flash_msg = "Saved!";
                    flash_timer = 15;
                    /* Stay in menu */
                    break;

                case MENU_OVERWRITE:
                    printf("InGameMenu: Overwrite Save\n");
                    save_sram();
                    flash_msg = "Saved!";
                    flash_timer = 15;
                    /* Stay in menu */
                    break;

                case MENU_DELETE:
                    printf("InGameMenu: Delete Save\n");
                    if (menu_delete_save()) {
                        has_save = false;
                        flash_msg = "Deleted!";
                        flash_timer = 15;
                        /* Rebuild menu without save-dependent options */
                        count = 0;
                        labels[count] = "Resume Game";      ids[count] = MENU_RESUME;    count++;
                        labels[count] = "Restart Game";     ids[count] = MENU_RESTART;   count++;
                        labels[count] = "Save Game";        ids[count] = MENU_SAVE;      count++;
                        labels[count] = "Exit Game";        ids[count] = MENU_EXIT;      count++;
                        if (selected >= count) selected = count - 1;
                    } else {
                        flash_msg = "Error!";
                        flash_timer = 15;
                    }
                    break;

                case MENU_EXIT:
                    printf("InGameMenu: Exit Game\n");
                    menu_active = false;
                    keep_running = false;
                    break;
            }
        }

        prev = state;
    }

    return keep_running;
}


static void SaveState()
{
    printf("Saving state.\n");

    odroid_input_battery_monitor_enabled_set(0);
    odroid_system_led_set(1);

    save_sram();

    odroid_system_led_set(0);
    odroid_input_battery_monitor_enabled_set(1);

    printf("Saving state OK.\n");
}

static void DoQuit()
{
    // Stop the NES emulation loop
    nes_poweroff();

    // Clear audio to prevent studdering
    odroid_audio_terminate();

    // Stop video task
    printf("DoQuit: stopping tasks.\n");
    {
        uint8_t *discard;
        while (xQueueReceive(vidQueue, &discard, 0) == pdTRUE) {}
        uint16_t* param = 1;
        xQueueOverwrite(vidQueue, &param);
        int timeout = 500;
        while (!exitVideoTaskFlag && --timeout > 0) { vTaskDelay(1); }
    }

    // state
    printf("DoQuit: Saving state.\n");
    SaveState();

    // Signal nofrendo_run to exit
    nofrendo_quit_flag = true;
}


static odroid_gamepad_state previousJoystickState;
static bool ignoreMenuButton;
static ushort powerFrameCount;

static int ConvertJoystickInput()
{
    if (ignoreMenuButton)
    {
        ignoreMenuButton = previousJoystickState.values[ODROID_INPUT_MENU];
    }


    odroid_gamepad_state state;
    odroid_input_gamepad_read(&state);

	int result = 0;


	// A
	if (!state.values[ODROID_INPUT_A])
	{
		result |= (1<<13);
	}

	// B
	if (!state.values[ODROID_INPUT_B])
	{
		result |= (1 << 14);
	}

	// select
	if (!state.values[ODROID_INPUT_SELECT])
		result |= (1 << 0);

	// start
	if (!state.values[ODROID_INPUT_START])
		result |= (1 << 3);

	// right
	if (!state.values[ODROID_INPUT_RIGHT])
			result |= (1 << 5);

	// left
	if (!state.values[ODROID_INPUT_LEFT])
			result |= (1 << 7);

	// up
	if (!state.values[ODROID_INPUT_UP])
			result |= (1 << 4);

	// down
	if (!state.values[ODROID_INPUT_DOWN])
			result |= (1 << 6);


    if (!previousJoystickState.values[ODROID_INPUT_VOLUME] && state.values[ODROID_INPUT_VOLUME])
    {
        show_volume_overlay();
    }

    if (!ignoreMenuButton && previousJoystickState.values[ODROID_INPUT_MENU] && state.values[ODROID_INPUT_MENU])
    {
        ++powerFrameCount;
    }
    else
    {
        powerFrameCount = 0;
    }

    // Long-press menu = show in-game menu
    if (powerFrameCount > 30 * 2)
    {
        powerFrameCount = 0;
        if (!show_ingame_menu()) DoQuit();
    }

    // Short-press menu = show in-game menu
    if (!ignoreMenuButton && previousJoystickState.values[ODROID_INPUT_MENU] && !state.values[ODROID_INPUT_MENU])
    {
        if (!show_ingame_menu()) DoQuit();
    }


    // Scaling
    if (state.values[ODROID_INPUT_START] && !previousJoystickState.values[ODROID_INPUT_RIGHT] && state.values[ODROID_INPUT_RIGHT])
    {
        scaling_enabled = !scaling_enabled;
        odroid_settings_ScaleDisabled_set(ODROID_SCALE_DISABLE_NES, scaling_enabled ? 0 : 1);
    }


    previousJoystickState = state;

	return result;
}


extern nes_t* console_nes;
extern nes6502_context cpu;

void osd_getinput(void)
{
	const int ev[16]={
			event_joypad1_select,0,0,event_joypad1_start,event_joypad1_up,event_joypad1_right,event_joypad1_down,event_joypad1_left,
			0,0,0,0,event_soft_reset,event_joypad1_a,event_joypad1_b,event_hard_reset
		};
	static int oldb=0xffff;
	int b=ConvertJoystickInput();
	int chg=b^oldb;
	int x;
	oldb=b;
	event_t evh;
//	printf("Input: %x\n", b);
	for (x=0; x<16; x++) {
		if (chg&1) {
			evh=event_get(ev[x]);
			if (evh) evh((b&1)?INP_STATE_BREAK:INP_STATE_MAKE);
		}
		chg>>=1;
		b>>=1;
	}
}

static void osd_freeinput(void)
{
}

void osd_getmouse(int *x, int *y, int *button)
{
}

/*
** Shutdown
*/

/* this is at the bottom, to eliminate warnings */
void osd_shutdown()
{
	osd_stopsound();
	osd_freeinput();

	/* Free NES framebuffers allocated in osd_init */
	if (lcdfb[0]) { heap_caps_free(lcdfb[0]); lcdfb[0] = NULL; }
	if (lcdfb[1]) { heap_caps_free(lcdfb[1]); lcdfb[1] = NULL; }
	lcdfb_write_idx = 0;

	/* Delete video queue */
	if (vidQueue) { vQueueDelete(vidQueue); vidQueue = NULL; }

#if CONFIG_SOUND_ENA
	/* Free audio frame buffer */
	if (audio_frame) { free(audio_frame); audio_frame = NULL; }
#endif
}

static int logprint(const char *string)
{
   return printf("%s", string);
}

/*
** Startup
*/
// Boot state overrides
bool forceConsoleReset = false;

int osd_init()
{
	log_chain_logfunc(logprint);

	if (osd_init_sound())
    {
        abort();
    }


    Volume = odroid_settings_Volume_get();

    scaling_enabled = odroid_settings_ScaleDisabled_get(ODROID_SCALE_DISABLE_NES) ? false : true;

    previousJoystickState = odroid_input_read_raw();
    ignoreMenuButton = previousJoystickState.values[ODROID_INPUT_MENU];


	ili9341_write_frame_nes(NULL, NULL, true);


	/* Allocate double-buffer for NES frame data in PSRAM (guard against re-entry) */
	if (!lcdfb[0]) lcdfb[0] = (uint8_t *)heap_caps_malloc(256 * 224, MALLOC_CAP_SPIRAM);
	if (!lcdfb[1]) lcdfb[1] = (uint8_t *)heap_caps_malloc(256 * 224, MALLOC_CAP_SPIRAM);
	if (!lcdfb[0] || !lcdfb[1]) {
		printf("osd_init: failed to allocate NES framebuffers!\n");
		abort();
	}
	lcdfb_write_idx = 0;

	if (!vidQueue) vidQueue = xQueueCreate(1, sizeof(bitmap_t *));
	exitVideoTaskFlag = false;
	xTaskCreatePinnedToCore(&videoTask, "videoTask", 4096, NULL, 5, NULL, 1);

    osd_initinput();

	return 0;
}
