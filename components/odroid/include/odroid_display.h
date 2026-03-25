/*
 * Odroid Display Compatibility Layer for ESP32-P4
 *
 * Provides the original ILI9341 API surface.
 * Internally renders to a 320×240 PSRAM framebuffer, then uses
 * PPA hardware to rotate (270° CCW) and scale (2×) for the
 * 480×800 MIPI DSI display.
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the display subsystem.
 * Allocates the 320×240 virtual framebuffer in PSRAM,
 * initializes the ST7701 MIPI DSI LCD (if not already done),
 * and clears the screen to black.
 */
void ili9341_init(void);

/**
 * @brief Write a rectangular region of RGB565 pixels.
 * Copies the pixel data into the virtual 320×240 framebuffer.
 * Call display_flush() to push changes to the physical LCD.
 *
 * @param x     Left coordinate (0..319)
 * @param y     Top coordinate (0..239)
 * @param w     Width in pixels
 * @param h     Height in pixels
 * @param data  Pointer to RGB565 pixel data (w*h elements)
 */
void ili9341_write_frame_rectangleLE(int x, int y, int w, int h, const uint16_t *data);

/**
 * @brief Clear the entire virtual framebuffer to a solid color.
 * @param color RGB565 color value
 */
void ili9341_clear(uint16_t color);

/**
 * @brief Flush the 320×240 framebuffer to the physical LCD.
 * Performs PPA rotate 270° CCW → scale 2× → display centered on 480×800.
 * Only flushes if the framebuffer has been modified since the last flush.
 */
void display_flush(void);

/**
 * @brief Force a flush even if the framebuffer hasn't been modified.
 */
void display_flush_force(void);

/**
 * @brief Set custom PPA scale factors for the rotate+scale pipeline.
 * Default is 2.0×2.0 (320×240 → 480×640). For example, 2.0×2.5
 * fills the full 480×800 LCD after 270° rotation.
 * @param sx  Horizontal scale factor (applied after rotation)
 * @param sy  Vertical scale factor (applied after rotation)
 */
void display_set_scale(float sx, float sy);

/**
 * @brief Check if LEDC backlight has been initialized.
 * @return true if initialized
 */
bool is_backlight_initialized(void);

/**
 * @brief Get a direct pointer to the 320×240 framebuffer (for advanced use).
 * @return Pointer to the RGB565 framebuffer (320*240 = 76,800 elements)
 */
uint16_t *display_get_framebuffer(void);

/**
 * @brief Get a pointer to the 320×240 emulator-scaled buffer.
 * Used by in-game menus. Draw with 320-pixel stride, then call display_emu_flush().
 */
uint16_t *display_get_emu_buffer(void);

/**
 * @brief Flush the 320×240 emulator buffer via PPA 2× scale + 270° rotate → 480×640 LCD.
 */
void display_emu_flush(void);

/**
 * @brief Draw raw RGB565 pixels directly to the LCD at portrait coordinates.
 * Thread-safe (takes/releases the display lock).
 * @param x  Portrait x position
 * @param y  Portrait y position
 * @param w  Width in pixels (portrait x direction)
 * @param h  Height in pixels (portrait y direction)
 * @param data  RGB565 pixel data (w*h elements, DMA-capable memory)
 */
void display_lcd_draw_raw(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                          const uint16_t *data);

/* ─── Emulator-specific display write functions ───────────────── */

/**
 * @brief Write a Game Boy framebuffer (160x144, direct RGB565).
 * Centers on the 320×240 virtual framebuffer and flushes.
 * @param buffer  RGB565 pixel data (160×144), or NULL to clear
 * @param scale   Scaling mode (ignored on P4 — PPA handles scaling)
 */
void ili9341_write_frame_gb(uint16_t *buffer, int scale);

/**
 * @brief Write a NES framebuffer (256x224, 8-bit palette-indexed).
 * @param buffer    8-bit indexed framebuffer (256×224), or NULL to clear
 * @param myPalette 256-entry RGB565 palette (already byte-swapped for HW)
 * @param scale     Scaling mode (ignored on P4)
 */
void ili9341_write_frame_nes(uint8_t *buffer, uint16_t *myPalette, uint8_t scale);

/**
 * @brief Write an SMS/Game Gear framebuffer (indexed).
 * SMS: 256×192  Game Gear: 160×144 (within 256-wide buffer at offset 48)
 * @param buffer      8-bit indexed framebuffer, or NULL to clear
 * @param color       Palette (RGB565)
 * @param isGameGear  True for Game Gear mode
 * @param scale       Scaling mode (ignored on P4)
 */
void ili9341_write_frame_sms(uint8_t *buffer, uint16_t color[], uint8_t isGameGear, uint8_t scale);

/**
 * @brief Write a C64-style indexed framebuffer (384x272, 8-bit palette indices).
 * Converts via palette to RGB565 and writes the center 320x240 region.
 */
void ili9341_write_frame_c64(uint8_t *buffer, uint16_t *palette);

/**
 * @brief Write an Atari 7800 framebuffer (320×240, 8-bit palette-indexed).
 * Converts via palette to RGB565 directly into the 320×240 framebuffer.
 * @param buffer    8-bit indexed framebuffer (320×240), or NULL to clear
 * @param palette   256-entry RGB565 palette
 */
void ili9341_write_frame_prosystem(uint8_t *buffer, uint16_t *palette);

/**
 * @brief Write an Atari Lynx framebuffer (160×102, pre-decoded RGB565).
 * PPA hardware-scales to 320×240 and flushes.
 * @param buffer  160×102 RGB565 pixels, or NULL to clear
 */
void ili9341_write_frame_lynx(const uint16_t *buffer);

/**
 * @brief Write a pre-rendered RGB565 framebuffer (320×240, LE byte-swapped).
 * Uses PPA hardware to scale 2× and rotate 270° in one operation.
 * @param buffer  320×240 RGB565 pixels in little-endian byte order, or NULL to clear
 */
void ili9341_write_frame_rgb565(const uint16_t *buffer);

/**
 * @brief Write a pre-rendered RGB565 framebuffer with byte-swap control.
 * @param buffer         320×240 RGB565 pixels, or NULL to clear
 * @param byte_swap_input  true if input is BE and PPA should byte-swap to LE
 */
void ili9341_write_frame_rgb565_ex(const uint16_t *buffer, bool byte_swap_input);

/**
 * @brief Write an arbitrary-size RGB565 framebuffer with PPA scale + rotate.
 * PPA scales by 'scale' and rotates 270°, centered on the 480×800 LCD.
 * Manages PPA buffer, border clearing, and display locking internally.
 * @param buffer          RGB565 pixels (in_w × in_h), or NULL to clear
 * @param in_w            Input width in pixels
 * @param in_h            Input height in pixels
 * @param scale           Scale factor (e.g. 2.0 for 2× integer scaling)
 * @param byte_swap_input true if PPA should byte-swap (BE→LE)
 */
void ili9341_write_frame_rgb565_custom(const uint16_t *buffer, uint16_t in_w,
                                        uint16_t in_h, float scale,
                                        bool byte_swap_input);

/** @brief Power off the display (no-op on P4 — LCD stays on). */
void ili9341_poweroff(void);

/** @brief Prepare the display for emulator use (no-op on P4). */
void ili9341_prepare(void);

/* ─── Display locking (per-emulator mutexes map to single mutex) ─ */
void odroid_display_lock(void);
void odroid_display_unlock(void);
void odroid_display_lock_gb_display(void);
void odroid_display_unlock_gb_display(void);
void odroid_display_lock_nes_display(void);
void odroid_display_unlock_nes_display(void);
void odroid_display_lock_sms_display(void);
void odroid_display_unlock_sms_display(void);

/* ─── Status screens ──────────────────────────────────────────── */
#define ODROID_SD_ERR_NOCARD  0
#define ODROID_SD_ERR_BADFILE 1
void odroid_display_show_sderr(int errNum);
void odroid_display_show_hourglass(void);
void odroid_display_show_splash(void);
void odroid_display_drain_spi(void);

/* Scaling disable keys (used with odroid_settings) */
#define ODROID_SCALE_DISABLE_GB  0
#define ODROID_SCALE_DISABLE_NES 1
#define ODROID_SCALE_DISABLE_SMS 2

#ifdef __cplusplus
}
#endif
