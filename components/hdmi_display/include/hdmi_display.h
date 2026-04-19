#pragma once

#include "esp_err.h"
#include <stdint.h>

typedef enum {
    HDMI_MODE_640x480 = 0,  /* 640x480 @60Hz, 40MHz pclk, FB=900KB */
    HDMI_MODE_800x600,      /* 800x600 @60Hz, 40MHz pclk, FB=1.4MB */
} hdmi_mode_t;

typedef struct {
    uint16_t h_res;
    uint16_t v_res;
    uint8_t  bpp;           /* Always 3 (RGB888) */
    void    *fb;            /* Framebuffer pointer — write RGB888 pixels here */
    uint32_t fb_size;       /* Framebuffer size in bytes */
} hdmi_display_t;

/**
 * Initialize the HDMI display pipeline:
 *   LDO → I2C → DSI bus → LT8912 → DPI panel → streaming.
 *
 * Uses GPIOs from Kconfig (CONFIG_LT8912_I2C_SDA/SCL/RESET_GPIO).
 * Framebuffer is in PSRAM, RGB888 byte order (Red = 0xFF,0x00,0x00).
 *
 * After esp_cache_msync(fb, fb_size, ESP_CACHE_MSYNC_FLAG_DIR_C2M),
 * pixels appear on screen at the next refresh.
 *
 * @param mode   Resolution mode
 * @param out    Filled with resolution, fb pointer, etc.
 * @return ESP_OK on success
 */
esp_err_t hdmi_display_init(hdmi_mode_t mode, hdmi_display_t *out);
