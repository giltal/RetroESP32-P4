/*
 * PPA Engine - Hardware 2D graphics accelerator wrapper for ESP32-P4
 * Provides easy-to-use APIs for PPA Scale-Rotate-Mirror, Blend, and Fill operations
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize PPA engine (registers SRM, Blend, and Fill clients)
 * @return ESP_OK on success
 */
esp_err_t ppa_engine_init(void);

/**
 * @brief Deinitialize PPA engine (unregisters all clients)
 */
void ppa_engine_deinit(void);

/* ========================= FILL Operations ========================= */

/**
 * @brief Fill a rectangular region in a buffer with a solid RGB565 color (hardware accelerated)
 *
 * @param buf         Output buffer (must be DMA-capable, cache-line aligned)
 * @param buf_size    Total output buffer size in bytes
 * @param pic_w       Width of the full picture in pixels
 * @param pic_h       Height of the full picture in pixels
 * @param x           X offset of the fill region
 * @param y           Y offset of the fill region
 * @param fill_w      Width of the fill region in pixels
 * @param fill_h      Height of the fill region in pixels
 * @param rgb565_color  Color in RGB565 format
 * @return ESP_OK on success
 */
esp_err_t ppa_fill_rect_rgb565(void *buf, size_t buf_size,
                                uint32_t pic_w, uint32_t pic_h,
                                uint32_t x, uint32_t y,
                                uint32_t fill_w, uint32_t fill_h,
                                uint16_t rgb565_color);

/**
 * @brief Create a solid-color RGB565 framebuffer using PPA fill (hardware accelerated)
 *
 * Allocates a DMA-capable PSRAM buffer and fills it with the given color.
 *
 * @param width       Width in pixels
 * @param height      Height in pixels
 * @param rgb565_color  Fill color in RGB565 format
 * @param[out] out_buf  Pointer to receive the allocated buffer (caller must free)
 * @param[out] out_size Pointer to receive the buffer size
 * @return ESP_OK on success
 */
esp_err_t ppa_create_filled_buffer(uint32_t width, uint32_t height,
                                    uint16_t rgb565_color,
                                    void **out_buf, size_t *out_size);

/* ========================= SRM Operations ========================= */

/**
 * @brief Scale an RGB565 image using PPA hardware acceleration
 *
 * @param in_buf       Input pixel buffer (RGB565)
 * @param in_w         Input image width
 * @param in_h         Input image height
 * @param out_w        Desired output width
 * @param out_h        Desired output height
 * @param[out] out_buf Pointer to receive the allocated output buffer (caller must free)
 * @param[out] out_size Pointer to receive the output buffer size
 * @return ESP_OK on success
 */
esp_err_t ppa_scale_rgb565(const void *in_buf, uint32_t in_w, uint32_t in_h,
                            uint32_t out_w, uint32_t out_h,
                            void **out_buf, size_t *out_size);

/**
 * @brief Rotate an RGB565 image using PPA hardware (0, 90, 180, 270 degrees CCW)
 *
 * @param in_buf       Input pixel buffer (RGB565)
 * @param in_w         Input image width
 * @param in_h         Input image height
 * @param angle_deg    Rotation angle: 0, 90, 180, or 270 (counter-clockwise)
 * @param[out] out_buf Pointer to receive the allocated output buffer (caller must free)
 * @param[out] out_size Pointer to receive the output buffer size
 * @param[out] out_w   Pointer to receive the output width
 * @param[out] out_h   Pointer to receive the output height
 * @return ESP_OK on success
 */
esp_err_t ppa_rotate_rgb565(const void *in_buf, uint32_t in_w, uint32_t in_h,
                             uint32_t angle_deg,
                             void **out_buf, size_t *out_size,
                             uint32_t *out_w, uint32_t *out_h);

/**
 * @brief Mirror an RGB565 image using PPA hardware
 *
 * @param in_buf       Input pixel buffer (RGB565)
 * @param in_w         Input image width
 * @param in_h         Input image height
 * @param mirror_x     Mirror horizontally
 * @param mirror_y     Mirror vertically
 * @param[out] out_buf Pointer to receive allocated output buffer (caller must free)
 * @param[out] out_size Pointer to receive the output buffer size
 * @return ESP_OK on success
 */
esp_err_t ppa_mirror_rgb565(const void *in_buf, uint32_t in_w, uint32_t in_h,
                             bool mirror_x, bool mirror_y,
                             void **out_buf, size_t *out_size);

/**
 * @brief Perform RGB channel swap on an RGB565 image using PPA hardware
 *
 * This is useful for BGR-to-RGB conversion on the ESP32-P4 MIPI DSI panel.
 *
 * @param in_buf       Input pixel buffer (RGB565)
 * @param in_w         Input image width
 * @param in_h         Input image height
 * @param[out] out_buf Pointer to receive allocated output buffer (caller must free)
 * @param[out] out_size Pointer to receive the output buffer size
 * @return ESP_OK on success
 */
esp_err_t ppa_rgb_swap_rgb565(const void *in_buf, uint32_t in_w, uint32_t in_h,
                               void **out_buf, size_t *out_size);

/* ========================= BLEND Operations ========================= */

/**
 * @brief Alpha-blend a foreground image onto a background image (both RGB565)
 *
 * @param bg_buf       Background pixel buffer (RGB565)
 * @param bg_w         Background width
 * @param bg_h         Background height
 * @param fg_buf       Foreground pixel buffer (RGB565)
 * @param fg_w         Foreground width
 * @param fg_h         Foreground height
 * @param fg_alpha     Foreground fixed alpha value (0-255, 255=opaque)
 * @param[out] out_buf Pointer to receive allocated output buffer (caller must free)
 * @param[out] out_size Pointer to receive the output buffer size
 * @return ESP_OK on success
 */
esp_err_t ppa_blend_rgb565(const void *bg_buf, uint32_t bg_w, uint32_t bg_h,
                            const void *fg_buf, uint32_t fg_w, uint32_t fg_h,
                            uint8_t fg_alpha,
                            void **out_buf, size_t *out_size);

/* =================== Combined Rotate+Scale (in-place) =================== */

/**
 * @brief Rotate and scale an RGB565 image in a single PPA operation, using a
 *        caller-provided output buffer (no allocation).
 *
 * This combines rotation and scaling into one PPA SRM call, avoiding any
 * intermediate buffer and per-frame heap allocation.
 *
 * @param in_buf       Input pixel buffer (RGB565, DMA-capable)
 * @param in_w         Input image width
 * @param in_h         Input image height
 * @param angle_deg    Rotation angle: 0, 90, 180, or 270 (counter-clockwise)
 * @param scale_x      Horizontal scale factor (e.g. 2.0 for 2×)
 * @param scale_y      Vertical scale factor
 * @param out_buf      Pre-allocated output buffer (DMA-capable, cache-line aligned)
 * @param out_buf_size Size of @p out_buf in bytes
 * @param[out] out_w   Pointer to receive the output width (may be NULL)
 * @param[out] out_h   Pointer to receive the output height (may be NULL)
 * @param byte_swap    Swap bytes of each RGB565 pixel in the output (BE→LE)
 * @return ESP_OK on success
 */
esp_err_t ppa_rotate_scale_rgb565_to(const void *in_buf, uint32_t in_w, uint32_t in_h,
                                      uint32_t angle_deg,
                                      float scale_x, float scale_y,
                                      void *out_buf, size_t out_buf_size,
                                      uint32_t *out_w, uint32_t *out_h,
                                      bool byte_swap);

#ifdef __cplusplus
}
#endif
