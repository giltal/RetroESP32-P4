/*
 * Odroid Display Compatibility Layer — ESP32-P4 Implementation
 *
 * Manages an 800×480 RGB565 framebuffer in PSRAM (native resolution).
 * On display_flush(), uses PPA hardware to:
 *   1. Rotate 270° CCW  (800×480 → 480×800)
 *   2. Draw on the 480×800 MIPI DSI LCD (1:1 pixel mapping, no scaling)
 */

#include "odroid_display.h"
#include "ppa_engine.h"
#include "pins_config.h"

#ifdef CONFIG_HDMI_OUTPUT
#include "hdmi_display.h"
#include "esp_cache.h"
#else
#include "st7701_lcd.h"
#endif

#include <string.h>
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "driver/ledc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "odroid_display";

#ifdef CONFIG_HDMI_OUTPUT
/* HDMI: 640×480 landscape, RGB888 (3 bytes/pixel) via LT8912 */
#define FB_W  640
#define FB_H  480
static hdmi_display_t s_hdmi_disp;
static bool s_hdmi_initialized = false;
#else
/* LCD: 800×480 landscape (rotated 270° to 480×800 portrait) */
#define FB_W  800
#define FB_H  480
#endif

#define FB_PIXELS (FB_W * FB_H)
#define FB_SIZE   (FB_PIXELS * sizeof(uint16_t))  /* internal drawing is always RGB565 */

static uint16_t *s_framebuffer = NULL;
static bool s_fb_dirty = false;
static bool s_backlight_init = false;

/* ─── Backlight (LEDC on GPIO23) — LCD only ───────────────────── */
#ifndef CONFIG_HDMI_OUTPUT
#define BL_GPIO       LCD_BK_LIGHT_GPIO
#define BL_LEDC_CH    LEDC_CHANNEL_0
#define BL_LEDC_TIMER LEDC_TIMER_0
#define BL_DUTY_RES   LEDC_TIMER_13_BIT
#define BL_DUTY_MAX   ((1 << 13) - 1)  /* 8191 */
#define BL_FREQ_HZ    5000

static void backlight_init(void)
{
    ledc_timer_config_t timer_cfg = {
        .speed_mode      = LEDC_LOW_SPEED_MODE,
        .duty_resolution = BL_DUTY_RES,
        .timer_num       = BL_LEDC_TIMER,
        .freq_hz         = BL_FREQ_HZ,
        .clk_cfg         = LEDC_AUTO_CLK,
    };
    ledc_timer_config(&timer_cfg);

    ledc_channel_config_t ch_cfg = {
        .gpio_num   = BL_GPIO,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel    = BL_LEDC_CH,
        .timer_sel  = BL_LEDC_TIMER,
        .duty       = BL_DUTY_MAX,  /* start at full brightness */
        .hpoint     = 0,
    };
    ledc_channel_config(&ch_cfg);
    ledc_fade_func_install(0);
    s_backlight_init = true;
    ESP_LOGI(TAG, "Backlight LEDC initialized on GPIO %d", BL_GPIO);
}
#endif /* !CONFIG_HDMI_OUTPUT */

/* ─── Pre-allocated PPA output buffer ──────────────────────────── */
#ifdef CONFIG_HDMI_OUTPUT
/* HDMI: PPA output is 640×480 RGB888 = 921,600 bytes.
 * But we write directly to the HDMI DPI framebuffer (s_hdmi_disp.fb),
 * so we only need a temporary PPA buffer for the launcher flush
 * (RGB565→RGB888 conversion). Emulators also use this. */
#define HDMI_OUT_W     640
#define HDMI_OUT_H     480
#define HDMI_OUT_BPP   3
#define HDMI_OUT_SIZE  (HDMI_OUT_W * HDMI_OUT_H * HDMI_OUT_BPP)  /* 921600 */
#define PPA_BUF_ALIGN  64
/* s_ppa_out_buf not needed for HDMI — we write directly to s_hdmi_disp.fb */
#else
/* LCD: Max PPA output: 480×800 (full LCD). Actual size depends on scale factors. */
#define PPA_OUT_MAX_W  480
#define PPA_OUT_MAX_H  800
#define PPA_OUT_MAX_SIZE (PPA_OUT_MAX_W * PPA_OUT_MAX_H * sizeof(uint16_t))  /* 768000 */
#define PPA_BUF_ALIGN 64
#define PPA_OUT_ALIGNED ((PPA_OUT_MAX_SIZE + PPA_BUF_ALIGN - 1) & ~(PPA_BUF_ALIGN - 1))

static void *s_ppa_out_buf = NULL;
static size_t s_ppa_out_size = 0;
#endif

/* Emulator standard resolution (all Pipeline A emulators scale to this) */
#define EMU_W 320
#define EMU_H 240
#define EMU_PIXELS (EMU_W * EMU_H)
#define EMU_SIZE   (EMU_PIXELS * sizeof(uint16_t))

/* Shared 320×240 intermediate buffer for Pipeline A emulators */
static uint16_t *s_emu_scaled = NULL;
#ifndef CONFIG_HDMI_OUTPUT
static bool s_emu_borders_cleared_a = false;
#endif

/* Configurable scale factors (1×1 = native resolution → 480×800) */
static float s_scale_x = 1.0f;
static float s_scale_y = 1.0f;

/* ─── Timing instrumentation ──────────────────────────────────── */
static int64_t s_timing_ppa_acc = 0;
static int64_t s_timing_lcd_acc = 0;
static int64_t s_timing_pal_acc = 0;
static int     s_timing_count   = 0;
#define TIMING_INTERVAL 60

/* ─── Display flush ───────────────────────────────────────────── */
void display_flush(void)
{
    if (!s_fb_dirty || !s_framebuffer) return;
    s_fb_dirty = false;

#ifdef CONFIG_HDMI_OUTPUT
    /* HDMI: PPA convert 640×480 RGB565 → RGB888 directly into HDMI DPI FB */
    if (!s_hdmi_initialized) return;
    int64_t t0 = esp_timer_get_time();
    esp_err_t ret = ppa_scale_rgb565_to_rgb888(
        s_framebuffer, FB_W, FB_H,
        1.0f, 1.0f,
        s_hdmi_disp.fb, s_hdmi_disp.fb_size,
        NULL, NULL, false);  /* DSI outputs RGB byte order */
    int64_t t1 = esp_timer_get_time();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "PPA RGB565→RGB888 flush failed (0x%x)", ret);
        return;
    }
    esp_cache_msync(s_hdmi_disp.fb, s_hdmi_disp.fb_size, ESP_CACHE_MSYNC_FLAG_DIR_C2M);
    int64_t t2 = esp_timer_get_time();
#else
    /* LCD: PPA rotate 270° + scale → push to ST7701 */
    /* Lazy-allocate the persistent PPA output buffer */
    if (!s_ppa_out_buf) {
        s_ppa_out_buf = heap_caps_aligned_calloc(
            PPA_BUF_ALIGN, 1, PPA_OUT_ALIGNED,
            MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA);
        if (!s_ppa_out_buf) {
            ESP_LOGE(TAG, "Failed to allocate PPA output buffer (%d bytes)", PPA_OUT_ALIGNED);
            return;
        }
        s_ppa_out_size = PPA_OUT_ALIGNED;
        ESP_LOGI(TAG, "PPA output buffer allocated: %d bytes", PPA_OUT_ALIGNED);
    }

    /* Single PPA SRM: rotate 270° + scale in one operation */
    uint32_t out_w = 0, out_h = 0;
    int64_t t0 = esp_timer_get_time();
    esp_err_t ret = ppa_rotate_scale_rgb565_to(
        s_framebuffer, FB_W, FB_H,
        270, s_scale_x, s_scale_y,
        s_ppa_out_buf, s_ppa_out_size,
        &out_w, &out_h, false);
    int64_t t1 = esp_timer_get_time();

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "PPA rotate+scale failed (0x%x)", ret);
        return;
    }

    /* Display centered on 480×800 at (0, 80) */
    uint16_t lcd_h = st7701_lcd_height();  /* 800 */
    uint16_t y_off = (lcd_h > out_h) ? (lcd_h - out_h) / 2 : 0;

    st7701_lcd_draw_rgb_bitmap(0, y_off, out_w, out_h, (const uint16_t *)s_ppa_out_buf);
    int64_t t2 = esp_timer_get_time();
#endif /* CONFIG_HDMI_OUTPUT */

    s_timing_ppa_acc += (t1 - t0);
    s_timing_lcd_acc += (t2 - t1);
    s_timing_count++;
    if (s_timing_count >= TIMING_INTERVAL) {
        printf("DISP TIMING (%d frames): PPA=%.1fms  LCD=%.1fms  PAL=%.1fms\n",
               s_timing_count,
               s_timing_ppa_acc / (s_timing_count * 1000.0f),
               s_timing_lcd_acc / (s_timing_count * 1000.0f),
               s_timing_pal_acc / (s_timing_count * 1000.0f));
        s_timing_ppa_acc = 0;
        s_timing_lcd_acc = 0;
        s_timing_pal_acc = 0;
        s_timing_count = 0;
    }
}

void display_flush_force(void)
{
    s_fb_dirty = true;
    display_flush();
}

void display_set_scale(float sx, float sy)
{
    s_scale_x = sx;
    s_scale_y = sy;
    ESP_LOGI(TAG, "PPA scale set to %.2fx%.2f", sx, sy);
}

/* ─── Pipeline A helper: 320×240 → PPA 2× + 270° → 480×640 LCD ─
 *
 * Called from Pipeline A functions that already hold the display lock.
 * Does the same thing as ili9341_write_frame_rgb565_ex() but without
 * lock/unlock, since the caller already owns the mutex.
 */
/* ─── Emulator flush helper ────────────────────────────────────── */
static void display_emu_flush_320x240(const uint16_t *buf, bool byte_swap)
{
#ifdef CONFIG_HDMI_OUTPUT
    /* HDMI: PPA scale 320×240 RGB565 → 640×480 RGB888 into HDMI FB */
    if (!s_hdmi_initialized) return;
    int64_t t0 = esp_timer_get_time();
    esp_err_t ret = ppa_scale_rgb565_to_rgb888(
        buf, EMU_W, EMU_H,
        (float)HDMI_OUT_W / EMU_W, (float)HDMI_OUT_H / EMU_H,
        s_hdmi_disp.fb, s_hdmi_disp.fb_size,
        NULL, NULL, false);  /* DSI outputs RGB byte order */
    int64_t t1 = esp_timer_get_time();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "PPA emu HDMI flush failed (0x%x)", ret);
        return;
    }
    esp_cache_msync(s_hdmi_disp.fb, s_hdmi_disp.fb_size, ESP_CACHE_MSYNC_FLAG_DIR_C2M);
    int64_t t2 = esp_timer_get_time();
    (void)byte_swap;
#else
    /* LCD: PPA 2× scale + 270° rotate → 480×640 */
    /* Lazy-allocate the persistent PPA output buffer */
    if (!s_ppa_out_buf) {
        s_ppa_out_buf = heap_caps_aligned_calloc(
            PPA_BUF_ALIGN, 1, PPA_OUT_ALIGNED,
            MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA);
        if (!s_ppa_out_buf) {
            ESP_LOGE(TAG, "Failed to allocate PPA output buffer (%d bytes)", PPA_OUT_ALIGNED);
            return;
        }
        s_ppa_out_size = PPA_OUT_ALIGNED;
    }

    /* Clear LCD border areas once (top/bottom 80 rows) */
    if (!s_emu_borders_cleared_a) {
        size_t border_size = 480 * 80 * sizeof(uint16_t);
        memset(s_ppa_out_buf, 0, border_size);
        st7701_lcd_draw_rgb_bitmap(0, 0, 480, 80, (const uint16_t *)s_ppa_out_buf);
        st7701_lcd_draw_rgb_bitmap(0, 720, 480, 80, (const uint16_t *)s_ppa_out_buf);
        s_emu_borders_cleared_a = true;
    }

    /* PPA: 320×240 → scale 2× + rotate 270° → 480×640 */
    uint32_t out_w = 0, out_h = 0;
    int64_t t0 = esp_timer_get_time();
    esp_err_t ret = ppa_rotate_scale_rgb565_to(
        buf, EMU_W, EMU_H,
        270, 2.0f, 2.0f,
        s_ppa_out_buf, s_ppa_out_size,
        &out_w, &out_h, byte_swap);
    int64_t t1 = esp_timer_get_time();

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "PPA emu 2x+rot failed (0x%x)", ret);
        return;
    }

    /* Center on 480×800 LCD: y_off = (800 - 640) / 2 = 80 */
    uint16_t lcd_h = st7701_lcd_height();
    uint16_t y_off = (lcd_h > out_h) ? (lcd_h - out_h) / 2 : 0;
    st7701_lcd_draw_rgb_bitmap(0, y_off, out_w, out_h, (const uint16_t *)s_ppa_out_buf);
    int64_t t2 = esp_timer_get_time();
#endif /* CONFIG_HDMI_OUTPUT */

    s_timing_ppa_acc += (t1 - t0);
    s_timing_lcd_acc += (t2 - t1);
    s_timing_count++;
    if (s_timing_count >= TIMING_INTERVAL) {
        printf("DISP TIMING (%d frames): PPA=%.1fms  LCD=%.1fms\n",
               s_timing_count,
               s_timing_ppa_acc / (s_timing_count * 1000.0f),
               s_timing_lcd_acc / (s_timing_count * 1000.0f));
        s_timing_ppa_acc = 0;
        s_timing_lcd_acc = 0;
        s_timing_pal_acc = 0;
        s_timing_count = 0;
    }
}

/* ─── ILI9341-compatible API ──────────────────────────────────── */

void ili9341_init(void)
{
    if (s_framebuffer) return;  /* already initialized */

    /* 768KB framebuffer requires PSRAM (won't fit internal SRAM) */
    s_framebuffer = (uint16_t *)heap_caps_aligned_calloc(
        64, 1, FB_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA);
    if (!s_framebuffer) {
        ESP_LOGE(TAG, "Failed to allocate %d byte framebuffer!", FB_SIZE);
        return;
    }
    ESP_LOGI(TAG, "Virtual framebuffer allocated in PSRAM: %dx%d (%d bytes)",
             FB_W, FB_H, FB_SIZE);

#ifdef CONFIG_HDMI_OUTPUT
    /* HDMI: Initialize the HDMI display (LT8912 via DSI) */
    if (!s_hdmi_initialized) {
        esp_err_t ret = hdmi_display_init(HDMI_MODE_640x480, &s_hdmi_disp);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "HDMI display init failed (0x%x)", ret);
            return;
        }
        s_hdmi_initialized = true;
        ESP_LOGI(TAG, "HDMI display initialized: %dx%d, fb=%p (%lu bytes)",
                 s_hdmi_disp.h_res, s_hdmi_disp.v_res,
                 s_hdmi_disp.fb, (unsigned long)s_hdmi_disp.fb_size);
    }
#else
    /* LCD: Initialize backlight */
    if (!s_backlight_init) {
        backlight_init();
    }
#endif
}

void ili9341_write_frame_rectangleLE(int x, int y, int w, int h, const uint16_t *data)
{
    if (!s_framebuffer || !data) return;

    /* Clip to framebuffer bounds and copy */
    for (int row = 0; row < h; row++) {
        int fb_y = y + row;
        if (fb_y < 0 || fb_y >= FB_H) continue;
        for (int col = 0; col < w; col++) {
            int fb_x = x + col;
            if (fb_x < 0 || fb_x >= FB_W) continue;
            s_framebuffer[fb_y * FB_W + fb_x] = data[row * w + col];
        }
    }
    s_fb_dirty = true;
}

void ili9341_clear(uint16_t color)
{
    if (!s_framebuffer) return;
    for (int i = 0; i < FB_PIXELS; i++) {
        s_framebuffer[i] = color;
    }
    s_fb_dirty = true;
}

bool is_backlight_initialized(void)
{
#ifdef CONFIG_HDMI_OUTPUT
    return s_hdmi_initialized;
#else
    return s_backlight_init;
#endif
}

uint16_t *display_get_framebuffer(void)
{
    return s_framebuffer;
}

uint16_t *display_get_emu_buffer(void)
{
    if (!s_emu_scaled) {
        s_emu_scaled = heap_caps_aligned_calloc(
            64, 1, EMU_SIZE, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
        if (!s_emu_scaled) {
            s_emu_scaled = heap_caps_aligned_calloc(
                64, 1, EMU_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA);
        }
    }
    return s_emu_scaled;
}

void display_emu_flush(void)
{
    if (s_emu_scaled) {
        display_emu_flush_320x240(s_emu_scaled, false);
    }
}

void display_lcd_draw_raw(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                          const uint16_t *data)
{
#ifdef CONFIG_HDMI_OUTPUT
    /* HDMI: no direct portrait draw — no-op (HDMI has no portrait mode) */
    (void)x; (void)y; (void)w; (void)h; (void)data;
#else
    odroid_display_lock();
    st7701_lcd_draw_rgb_bitmap(x, y, w, h, data);
    odroid_display_unlock();
#endif
}

/* ─── Display mutex for exclusive access ──────────────────────── */
static SemaphoreHandle_t s_display_mutex = NULL;

static void ensure_mutex(void)
{
    if (!s_display_mutex) {
        s_display_mutex = xSemaphoreCreateMutex();
        if (!s_display_mutex) abort();
    }
}

void odroid_display_lock(void)           { ensure_mutex(); xSemaphoreTake(s_display_mutex, portMAX_DELAY); }
void odroid_display_unlock(void)         { if (s_display_mutex) xSemaphoreGive(s_display_mutex); }
void odroid_display_lock_gb_display(void)   { odroid_display_lock(); }
void odroid_display_unlock_gb_display(void) { odroid_display_unlock(); }
void odroid_display_lock_nes_display(void)  { odroid_display_lock(); }
void odroid_display_unlock_nes_display(void){ odroid_display_unlock(); }
void odroid_display_lock_sms_display(void)  { odroid_display_lock(); }
void odroid_display_unlock_sms_display(void){ odroid_display_unlock(); }

/* ─── Game Boy display: 160×144 direct RGB565 ─────────────────── */
#define GAMEBOY_WIDTH  160
#define GAMEBOY_HEIGHT 144
#define GB_PIXELS      (GAMEBOY_WIDTH * GAMEBOY_HEIGHT)

/* Static DMA-capable temp buffer for GB input (allocated on first use) */
static uint16_t *s_gb_temp = NULL;

void ili9341_write_frame_gb(uint16_t *buffer, int scale)
{
    (void)scale;
    odroid_display_lock_gb_display();

    if (buffer == NULL) {
        ili9341_clear(0x0000);
    } else {
        /* Lazy-allocate DMA-capable temp buffer for PPA input */
        if (!s_gb_temp) {
            s_gb_temp = heap_caps_aligned_calloc(
                64, 1, GB_PIXELS * sizeof(uint16_t),
                MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA);
            if (!s_gb_temp) {
                ESP_LOGE(TAG, "GB temp buffer alloc failed");
                odroid_display_unlock_gb_display();
                return;
            }
        }

        /* Copy input into DMA-aligned temp buffer */
        memcpy(s_gb_temp, buffer, GB_PIXELS * sizeof(uint16_t));

        /* Lazy-allocate shared 320×240 intermediate buffer (prefer internal SRAM) */
        if (!s_emu_scaled) {
            s_emu_scaled = heap_caps_aligned_calloc(
                64, 1, EMU_SIZE, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
            if (!s_emu_scaled) {
                s_emu_scaled = heap_caps_aligned_calloc(
                    64, 1, EMU_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA);
            }
            if (!s_emu_scaled) { ESP_LOGE(TAG, "emu_scaled alloc failed"); odroid_display_unlock_gb_display(); return; }
        }

#ifdef CONFIG_HDMI_OUTPUT
        /* Software NN upscale 160×144 → 320×240 (PPA fractional scale leaves
         * bottom rows unfilled; software fill is exact and avoids artifacts) */
        for (int y = 0; y < EMU_H; y++) {
            int src_y = y * GAMEBOY_HEIGHT / EMU_H;
            const uint16_t *src_row = &s_gb_temp[src_y * GAMEBOY_WIDTH];
            uint16_t *dst_row = &s_emu_scaled[y * EMU_W];
            for (int x = 0; x < EMU_W; x++) {
                dst_row[x] = src_row[x * GAMEBOY_WIDTH / EMU_W];
            }
        }
        esp_cache_msync(s_emu_scaled, EMU_SIZE, ESP_CACHE_MSYNC_FLAG_DIR_C2M);
#else
        /* PPA scale 160×144 → 320×240 */
        float sx = (float)EMU_W / GAMEBOY_WIDTH;   /* 2.0 */
        float sy = (float)EMU_H / GAMEBOY_HEIGHT;  /* 1.6667 */
        uint32_t out_w = 0, out_h = 0;
        esp_err_t ret = ppa_rotate_scale_rgb565_to(
            s_gb_temp, GAMEBOY_WIDTH, GAMEBOY_HEIGHT,
            0, sx, sy,
            s_emu_scaled, EMU_SIZE,
            &out_w, &out_h, false);

        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "PPA GB scale failed: %s", esp_err_to_name(ret));
            odroid_display_unlock_gb_display();
            return;
        }
#endif

        display_emu_flush_320x240(s_emu_scaled, false);
    }

    odroid_display_unlock_gb_display();
}

/* ─── NES display: 256×224, 8-bit indexed, 256-entry palette ──── */
#define NES_GAME_WIDTH  256
#define NES_GAME_HEIGHT 224
#define NES_PIXELS      (NES_GAME_WIDTH * NES_GAME_HEIGHT)

/* Static DMA-capable temp buffer for NES 256×224 RGB565 */
static uint16_t *s_nes_temp = NULL;

void ili9341_write_frame_nes(uint8_t *buffer, uint16_t *myPalette, uint8_t scale)
{
    (void)scale;
    odroid_display_lock_nes_display();

    if (buffer == NULL) {
        ili9341_clear(0x0000);
    } else {
        /* Lazy-allocate DMA-capable temp buffer */
        if (!s_nes_temp) {
            s_nes_temp = heap_caps_aligned_calloc(
                64, 1, NES_PIXELS * sizeof(uint16_t),
                MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA);
            if (!s_nes_temp) {
                ESP_LOGE(TAG, "NES temp buffer alloc failed");
                odroid_display_unlock_nes_display();
                return;
            }
        }

        /* Palette conversion: 8-bit indexed → RGB565 (byte-swapped to LE) */
        for (int i = 0; i < NES_PIXELS; i++) {
            uint16_t pixel = myPalette[buffer[i]];
            s_nes_temp[i] = (pixel >> 8) | (pixel << 8);
        }

        /* Lazy-allocate shared 320×240 intermediate buffer (prefer internal SRAM) */
        if (!s_emu_scaled) {
            s_emu_scaled = heap_caps_aligned_calloc(
                64, 1, EMU_SIZE, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
            if (!s_emu_scaled) {
                s_emu_scaled = heap_caps_aligned_calloc(
                    64, 1, EMU_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA);
            }
            if (!s_emu_scaled) { ESP_LOGE(TAG, "emu_scaled alloc failed"); odroid_display_unlock_nes_display(); return; }
        }

        /* PPA scale 256×224 → 320×240 */
        float sx = (float)EMU_W / NES_GAME_WIDTH;   /* 1.25 */
        float sy = (float)EMU_H / NES_GAME_HEIGHT;  /* 1.0714 */
        uint32_t out_w = 0, out_h = 0;
        esp_err_t ret = ppa_rotate_scale_rgb565_to(
            s_nes_temp, NES_GAME_WIDTH, NES_GAME_HEIGHT,
            0, sx, sy,
            s_emu_scaled, EMU_SIZE,
            &out_w, &out_h, false);

        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "PPA NES scale failed: %s", esp_err_to_name(ret));
            odroid_display_unlock_nes_display();
            return;
        }

        display_emu_flush_320x240(s_emu_scaled, false);
    }

    odroid_display_unlock_nes_display();
}

/* ─── SMS/Game Gear display: 8-bit indexed → PPA-scaled ───────── */
#define SMS_WIDTH       256
#define SMS_HEIGHT      192
#define GAMEGEAR_WIDTH  160
#define GAMEGEAR_HEIGHT 144
#define PIXEL_MASK      0x1F
#define SMS_MAX_PIXELS  (SMS_WIDTH * SMS_HEIGHT)  /* larger of SMS / GG */

/* Static DMA-capable temp buffer (sized for the larger SMS resolution) */
static uint16_t *s_sms_temp = NULL;

void ili9341_write_frame_sms(uint8_t *buffer, uint16_t color[], uint8_t isGameGear, uint8_t scale)
{
    (void)scale;
    odroid_display_lock_sms_display();

    if (buffer == NULL) {
        ili9341_clear(0x0000);
    } else {
        /* Lazy-allocate DMA-capable temp buffer */
        if (!s_sms_temp) {
            s_sms_temp = heap_caps_aligned_calloc(
                64, 1, SMS_MAX_PIXELS * sizeof(uint16_t),
                MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA);
            if (!s_sms_temp) {
                ESP_LOGE(TAG, "SMS temp buffer alloc failed");
                odroid_display_unlock_sms_display();
                return;
            }
        }

        const int src_w      = isGameGear ? GAMEGEAR_WIDTH  : SMS_WIDTH;
        const int src_h      = isGameGear ? GAMEGEAR_HEIGHT : SMS_HEIGHT;
        const int src_stride = isGameGear ? 256 : SMS_WIDTH;
        const int src_x_off  = isGameGear ? 48  : 0;

        /* Palette conversion: 8-bit indexed → RGB565 into temp buffer */
        for (int y = 0; y < src_h; y++) {
            const uint8_t *src_row = &buffer[y * src_stride + src_x_off];
            uint16_t *dst_row = &s_sms_temp[y * src_w];
            for (int x = 0; x < src_w; x++) {
                dst_row[x] = color[src_row[x] & PIXEL_MASK];
            }
        }

        /* Lazy-allocate shared 320×240 intermediate buffer (prefer internal SRAM) */
        if (!s_emu_scaled) {
            s_emu_scaled = heap_caps_aligned_calloc(
                64, 1, EMU_SIZE, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
            if (!s_emu_scaled) {
                s_emu_scaled = heap_caps_aligned_calloc(
                    64, 1, EMU_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA);
            }
            if (!s_emu_scaled) { ESP_LOGE(TAG, "emu_scaled alloc failed"); odroid_display_unlock_sms_display(); return; }
        }

#ifdef CONFIG_HDMI_OUTPUT
        if (isGameGear) {
            /* Software NN upscale 160×144 → 320×240 (PPA fractional scale
             * leaves bottom rows unfilled; software fill is exact) */
            for (int y = 0; y < EMU_H; y++) {
                int src_y = y * GAMEGEAR_HEIGHT / EMU_H;
                const uint16_t *sr = &s_sms_temp[src_y * GAMEGEAR_WIDTH];
                uint16_t *dr = &s_emu_scaled[y * EMU_W];
                for (int x = 0; x < EMU_W; x++) {
                    dr[x] = sr[x * GAMEGEAR_WIDTH / EMU_W];
                }
            }
            esp_cache_msync(s_emu_scaled, EMU_SIZE, ESP_CACHE_MSYNC_FLAG_DIR_C2M);
        } else {
            /* SMS: 256×192 → 320×240 (exact 1.25× both axes, PPA handles it) */
            float sx = (float)EMU_W / src_w;
            float sy = (float)EMU_H / src_h;
            uint32_t out_w = 0, out_h = 0;
            esp_err_t ret = ppa_rotate_scale_rgb565_to(
                s_sms_temp, src_w, src_h,
                0, sx, sy,
                s_emu_scaled, EMU_SIZE,
                &out_w, &out_h, false);
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "PPA SMS scale failed: %s", esp_err_to_name(ret));
                odroid_display_unlock_sms_display();
                return;
            }
        }
#else
        /* PPA scale src_w×src_h → 320×240 */
        float sx = (float)EMU_W / src_w;
        float sy = (float)EMU_H / src_h;
        uint32_t out_w = 0, out_h = 0;
        esp_err_t ret = ppa_rotate_scale_rgb565_to(
            s_sms_temp, src_w, src_h,
            0, sx, sy,
            s_emu_scaled, EMU_SIZE,
            &out_w, &out_h, false);

        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "PPA SMS scale failed: %s", esp_err_to_name(ret));
            odroid_display_unlock_sms_display();
            return;
        }
#endif

        display_emu_flush_320x240(s_emu_scaled, false);
    }

    odroid_display_unlock_sms_display();
}

/* ─── C64-specific display function ───────────────────────────── */
void ili9341_write_frame_c64(uint8_t *buffer, uint16_t *palette)
{
    const int C64_DISPLAY_X = 0x180; /* 384 */
    const int C64_DISPLAY_Y = 0x110; /* 272 */

    odroid_display_lock();

    if (buffer == NULL) {
        ili9341_clear(0x0000);
    } else {
        /* Lazy-allocate shared 320×240 intermediate buffer (prefer internal SRAM) */
        if (!s_emu_scaled) {
            s_emu_scaled = heap_caps_aligned_calloc(
                64, 1, EMU_SIZE, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
            if (!s_emu_scaled) {
                s_emu_scaled = heap_caps_aligned_calloc(
                    64, 1, EMU_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA);
            }
            if (!s_emu_scaled) { ESP_LOGE(TAG, "emu_scaled alloc failed"); odroid_display_unlock(); return; }
        }

        /* Crop 384×272 → center 320×240 with palette conversion */
        const int offX = (C64_DISPLAY_X - EMU_W) / 2; /* (384-320)/2 = 32 */
        const int offY = (C64_DISPLAY_Y - EMU_H) / 2; /* (272-240)/2 = 16 */

        for (int y = 0; y < EMU_H; ++y) {
            int src_base = (y + offY) * C64_DISPLAY_X + offX;
            int dst_base = y * EMU_W;
            for (int x = 0; x < EMU_W; ++x) {
                s_emu_scaled[dst_base + x] = palette[buffer[src_base + x]];
            }
        }

        display_emu_flush_320x240(s_emu_scaled, false);
    }

    odroid_display_unlock();
}

/* ─── Atari 7800 / PCE display: 320×240 8-bit indexed → Pipeline B ── */
void ili9341_write_frame_prosystem(uint8_t *buffer, uint16_t *palette)
{
    odroid_display_lock();

    if (buffer == NULL) {
        ili9341_clear(0x0000);
    } else {
        /* Lazy-allocate shared 320×240 intermediate buffer (prefer internal SRAM to reduce PSRAM contention) */
        if (!s_emu_scaled) {
            s_emu_scaled = heap_caps_aligned_calloc(
                64, 1, EMU_SIZE, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
            if (!s_emu_scaled) {
                s_emu_scaled = heap_caps_aligned_calloc(
                    64, 1, EMU_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA);
            }
            if (!s_emu_scaled) { ESP_LOGE(TAG, "emu_scaled alloc failed"); odroid_display_unlock(); return; }
        }

        int64_t tp0 = esp_timer_get_time();
        /* Palette lookup: 320×240 8-bit indexed → RGB565 into s_emu_scaled */
        const uint32_t *in32  = (const uint32_t *)buffer;
        uint32_t       *out32 = (uint32_t *)s_emu_scaled;
        for (int i = 0; i < EMU_PIXELS / 4; i++) {
            uint32_t pix4 = in32[i];
            uint16_t p0 = palette[(pix4 >>  0) & 0xFF];
            uint16_t p1 = palette[(pix4 >>  8) & 0xFF];
            uint16_t p2 = palette[(pix4 >> 16) & 0xFF];
            uint16_t p3 = palette[(pix4 >> 24) & 0xFF];
            out32[i * 2]     = p0 | ((uint32_t)p1 << 16);
            out32[i * 2 + 1] = p2 | ((uint32_t)p3 << 16);
        }
        s_timing_pal_acc += (esp_timer_get_time() - tp0);

        display_emu_flush_320x240(s_emu_scaled, false);
    }

    odroid_display_unlock();
}

/* ─── Atari Lynx display: 160×102 RGB565 → single PPA to display ──── */
#define LYNX_GAME_WIDTH  160
#define LYNX_GAME_HEIGHT 102
#define LYNX_PIXELS      (LYNX_GAME_WIDTH * LYNX_GAME_HEIGHT)

static uint16_t *s_lynx_temp = NULL;

void ili9341_write_frame_lynx(const uint16_t *buffer)
{
    odroid_display_lock();

    if (buffer == NULL) {
        ili9341_clear(0x0000);
    } else {
        if (!s_lynx_temp) {
            s_lynx_temp = heap_caps_aligned_calloc(
                64, 1, LYNX_PIXELS * sizeof(uint16_t),
                MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA);
            if (!s_lynx_temp) {
                ESP_LOGE(TAG, "Lynx temp buffer alloc failed");
                odroid_display_unlock();
                return;
            }
        }

        memcpy(s_lynx_temp, buffer, LYNX_PIXELS * sizeof(uint16_t));

#ifdef CONFIG_HDMI_OUTPUT
        /* HDMI: single PPA scale 160×102 → 640×480 RGB888 */
        if (!s_hdmi_initialized) { odroid_display_unlock(); return; }
        float sx = (float)HDMI_OUT_W / LYNX_GAME_WIDTH;
        float sy = (float)HDMI_OUT_H / LYNX_GAME_HEIGHT;
        esp_err_t ret = ppa_scale_rgb565_to_rgb888(
            s_lynx_temp, LYNX_GAME_WIDTH, LYNX_GAME_HEIGHT,
            sx, sy,
            s_hdmi_disp.fb, s_hdmi_disp.fb_size,
            NULL, NULL, false);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "PPA Lynx HDMI scale failed (0x%x)", ret);
            odroid_display_unlock();
            return;
        }
        esp_cache_msync(s_hdmi_disp.fb, s_hdmi_disp.fb_size, ESP_CACHE_MSYNC_FLAG_DIR_C2M);
#else
        /* LCD: single PPA scale 160×102 → 800×480 into framebuffer, then flush */
        float sx = (float)FB_W / LYNX_GAME_WIDTH;
        float sy = (float)FB_H / LYNX_GAME_HEIGHT;
        uint32_t out_w = 0, out_h = 0;
        esp_err_t ret = ppa_rotate_scale_rgb565_to(
            s_lynx_temp, LYNX_GAME_WIDTH, LYNX_GAME_HEIGHT,
            0, sx, sy,
            s_framebuffer, FB_SIZE,
            &out_w, &out_h, false);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "PPA Lynx scale failed: %s", esp_err_to_name(ret));
            odroid_display_unlock();
            return;
        }
        s_fb_dirty = true;
        display_flush();
#endif
    }

    odroid_display_unlock();
}

/* ─── Generic RGB565 display: 320×240 emulator output ────────── */
/*
 * Optimized path: PPA hardware does 2× scale + 270° rotation in one
 * operation directly from the 320×240 input → 480×640 output.
 * This avoids the intermediate 800×480 framebuffer, CPU 2× scaling,
 * border clearing, and the separate PPA rotation of the full 800×480.
 *
 * Input can be LE (pre-swapped by caller) or BE (native emulator output).
 * When byte_swap_input is set, PPA hardware swaps bytes during processing.
 */

#ifndef CONFIG_HDMI_OUTPUT
static bool s_emu_borders_cleared = false;
#endif

void ili9341_write_frame_rgb565_ex(const uint16_t *buffer, bool byte_swap_input)
{
    odroid_display_lock();

    if (buffer == NULL) {
        ili9341_clear(0x0000);
        display_flush();
#ifndef CONFIG_HDMI_OUTPUT
        s_emu_borders_cleared = false;
#endif
        odroid_display_unlock();
        return;
    }

#ifdef CONFIG_HDMI_OUTPUT
    /* HDMI: PPA scale 320×240 RGB565 → 640×480 RGB888 directly into HDMI FB */
    display_emu_flush_320x240(buffer, byte_swap_input);
#else
    /* LCD: PPA 2× scale + 270° rotate → push to ST7701 */
    if (!s_ppa_out_buf) {
        s_ppa_out_buf = heap_caps_aligned_calloc(
            PPA_BUF_ALIGN, 1, PPA_OUT_ALIGNED,
            MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA);
        if (!s_ppa_out_buf) {
            ESP_LOGE(TAG, "Failed to allocate PPA output buffer (%d bytes)", PPA_OUT_ALIGNED);
            odroid_display_unlock();
            return;
        }
        s_ppa_out_size = PPA_OUT_ALIGNED;
    }

    if (!s_emu_borders_cleared) {
        size_t border_size = 480 * 80 * sizeof(uint16_t);
        memset(s_ppa_out_buf, 0, border_size);
        st7701_lcd_draw_rgb_bitmap(0, 0, 480, 80, (const uint16_t *)s_ppa_out_buf);
        st7701_lcd_draw_rgb_bitmap(0, 720, 480, 80, (const uint16_t *)s_ppa_out_buf);
        s_emu_borders_cleared = true;
    }

    uint32_t out_w = 0, out_h = 0;
    int64_t t0 = esp_timer_get_time();
    esp_err_t ret = ppa_rotate_scale_rgb565_to(
        buffer, EMU_W, EMU_H,
        270, 2.0f, 2.0f,
        s_ppa_out_buf, s_ppa_out_size,
        &out_w, &out_h, byte_swap_input);
    int64_t t1 = esp_timer_get_time();

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "PPA emu rotate+scale failed (0x%x)", ret);
        odroid_display_unlock();
        return;
    }

    uint16_t lcd_h = st7701_lcd_height();
    uint16_t y_off = (lcd_h > out_h) ? (lcd_h - out_h) / 2 : 0;
    st7701_lcd_draw_rgb_bitmap(0, y_off, out_w, out_h, (const uint16_t *)s_ppa_out_buf);
    int64_t t2 = esp_timer_get_time();

    s_timing_ppa_acc += (t1 - t0);
    s_timing_lcd_acc += (t2 - t1);
    s_timing_count++;
    if (s_timing_count >= TIMING_INTERVAL) {
        printf("DISP TIMING (%d frames): PPA=%.1fms  LCD=%.1fms\n",
               s_timing_count,
               s_timing_ppa_acc / (s_timing_count * 1000.0f),
               s_timing_lcd_acc / (s_timing_count * 1000.0f));
        s_timing_ppa_acc = 0;
        s_timing_lcd_acc = 0;
        s_timing_pal_acc = 0;
        s_timing_count = 0;
    }
#endif /* CONFIG_HDMI_OUTPUT */

    odroid_display_unlock();
}

/* Backward-compatible wrapper: caller has already byte-swapped to LE */
void ili9341_write_frame_rgb565(const uint16_t *buffer)
{
    ili9341_write_frame_rgb565_ex(buffer, false);
}

/* ─── Custom-size RGB565 frame writer (PPA scale + rotate) ───── */
#ifndef CONFIG_HDMI_OUTPUT
static bool s_custom_borders_cleared = false;
#endif

void ili9341_write_frame_rgb565_custom(const uint16_t *buffer, uint16_t in_w,
                                        uint16_t in_h, float scale,
                                        bool byte_swap_input)
{
    odroid_display_lock();

    if (buffer == NULL) {
        ili9341_clear(0x0000);
        display_flush();
#ifndef CONFIG_HDMI_OUTPUT
        s_custom_borders_cleared = false;
#endif
        odroid_display_unlock();
        return;
    }

#ifdef CONFIG_HDMI_OUTPUT
    /* HDMI: PPA scale in_w×in_h RGB565 → 640×480 RGB888 directly into HDMI FB */
    if (!s_hdmi_initialized) { odroid_display_unlock(); return; }
    float sx = (float)HDMI_OUT_W / in_w;
    float sy = (float)HDMI_OUT_H / in_h;
    esp_err_t ret = ppa_scale_rgb565_to_rgb888(
        buffer, in_w, in_h,
        sx, sy,
        s_hdmi_disp.fb, s_hdmi_disp.fb_size,
        NULL, NULL, false);  /* DSI outputs RGB byte order */
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "PPA custom HDMI scale failed (0x%x)", ret);
        odroid_display_unlock();
        return;
    }
    esp_cache_msync(s_hdmi_disp.fb, s_hdmi_disp.fb_size, ESP_CACHE_MSYNC_FLAG_DIR_C2M);
    (void)scale; (void)byte_swap_input;
#else
    /* LCD: PPA scale + rotate 270° → push to ST7701 */
    if (!s_ppa_out_buf) {
        s_ppa_out_buf = heap_caps_aligned_calloc(
            PPA_BUF_ALIGN, 1, PPA_OUT_ALIGNED,
            MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA);
        if (!s_ppa_out_buf) {
            ESP_LOGE(TAG, "Failed to allocate PPA output buffer (%d bytes)", PPA_OUT_ALIGNED);
            odroid_display_unlock();
            return;
        }
        s_ppa_out_size = PPA_OUT_ALIGNED;
    }

    /* Compute output dimensions after scale + 270° rotation */
    uint16_t out_w_exp = (uint16_t)(in_h * scale);
    uint16_t out_h_exp = (uint16_t)(in_w * scale);
    uint16_t lcd_w = st7701_lcd_width();
    uint16_t lcd_h = st7701_lcd_height();
    uint16_t x_off = (lcd_w > out_w_exp) ? (lcd_w - out_w_exp) / 2 : 0;
    uint16_t y_off = (lcd_h > out_h_exp) ? (lcd_h - out_h_exp) / 2 : 0;

    if (!s_custom_borders_cleared) {
        ili9341_clear(0x0000);
        display_flush_force();
        s_custom_borders_cleared = true;
    }

    uint32_t out_w = 0, out_h = 0;
    esp_err_t ret = ppa_rotate_scale_rgb565_to(
        buffer, in_w, in_h,
        270, scale, scale,
        s_ppa_out_buf, s_ppa_out_size,
        &out_w, &out_h, byte_swap_input);

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "PPA custom rotate+scale failed (0x%x)", ret);
        odroid_display_unlock();
        return;
    }

    st7701_lcd_draw_rgb_bitmap(x_off, y_off, out_w, out_h,
                               (const uint16_t *)s_ppa_out_buf);
#endif /* CONFIG_HDMI_OUTPUT */
    odroid_display_unlock();
}

/* ─── Misc display functions ──────────────────────────────────── */
void ili9341_poweroff(void)
{
#ifndef CONFIG_HDMI_OUTPUT
    /* Turn off backlight to avoid white flash during OTA reboot */
    if (s_backlight_init) {
        ledc_set_duty(LEDC_LOW_SPEED_MODE, BL_LEDC_CH, 0);
        ledc_update_duty(LEDC_LOW_SPEED_MODE, BL_LEDC_CH);
    }
#endif
}

void ili9341_prepare(void)
{
    /* No-op on ESP32-P4 — LCD is already initialized */
}

void odroid_display_show_sderr(int errNum)
{
    (void)errNum;
    ESP_LOGE(TAG, "SD card error: %d", errNum);
    ili9341_clear(0xF800); /* Red screen */
    display_flush();
}

void odroid_display_show_hourglass(void)
{
    ESP_LOGI(TAG, "Hourglass (loading) indicator shown");
}

void odroid_display_show_splash(void)
{
    ESP_LOGI(TAG, "Splash screen (no-op on P4)");
}

void odroid_display_drain_spi(void)
{
    /* No-op — no SPI on P4 */
}

