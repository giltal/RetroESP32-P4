/*
 * PPA Engine - Hardware 2D graphics accelerator wrapper for ESP32-P4
 * Implements Scale-Rotate-Mirror, Blend, and Fill using the PPA peripheral
 */
#include "ppa_engine.h"
#include "driver/ppa.h"
#include "hal/ppa_types.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include <string.h>

static const char *TAG = "ppa_engine";

/* PPA client handles */
static ppa_client_handle_t s_ppa_srm_client   = NULL;
static ppa_client_handle_t s_ppa_blend_client  = NULL;
static ppa_client_handle_t s_ppa_fill_client   = NULL;

/* Cache-line alignment for DMA buffers (64 bytes typical on ESP32-P4) */
static size_t s_buf_align = 64;

/**
 * @brief Allocate a DMA-capable, cache-line-aligned buffer in PSRAM
 */
static void *ppa_alloc_buf(size_t size)
{
    /* Round up size to alignment */
    size_t aligned_size = (size + s_buf_align - 1) & ~(s_buf_align - 1);
    void *buf = heap_caps_aligned_calloc(s_buf_align, 1, aligned_size,
                                          MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA);
    if (!buf) {
        ESP_LOGE(TAG, "Failed to allocate PPA buffer (%u bytes)", (unsigned)aligned_size);
    }
    return buf;
}

esp_err_t ppa_engine_init(void)
{
    esp_err_t ret;

    ESP_LOGI(TAG, "PPA buffer alignment: %u bytes", (unsigned)s_buf_align);

    /* Register SRM client */
    ppa_client_config_t srm_cfg = {
        .oper_type = PPA_OPERATION_SRM,
        .max_pending_trans_num = 1,
    };
    ret = ppa_register_client(&srm_cfg, &s_ppa_srm_client);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register PPA SRM client: %s", esp_err_to_name(ret));
        return ret;
    }

    /* Register Blend client */
    ppa_client_config_t blend_cfg = {
        .oper_type = PPA_OPERATION_BLEND,
        .max_pending_trans_num = 1,
    };
    ret = ppa_register_client(&blend_cfg, &s_ppa_blend_client);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register PPA Blend client: %s", esp_err_to_name(ret));
        ppa_unregister_client(s_ppa_srm_client);
        s_ppa_srm_client = NULL;
        return ret;
    }

    /* Register Fill client */
    ppa_client_config_t fill_cfg = {
        .oper_type = PPA_OPERATION_FILL,
        .max_pending_trans_num = 1,
    };
    ret = ppa_register_client(&fill_cfg, &s_ppa_fill_client);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register PPA Fill client: %s", esp_err_to_name(ret));
        ppa_unregister_client(s_ppa_blend_client);
        ppa_unregister_client(s_ppa_srm_client);
        s_ppa_blend_client = NULL;
        s_ppa_srm_client = NULL;
        return ret;
    }

    ESP_LOGI(TAG, "PPA 2D engine initialized (SRM + Blend + Fill)");
    return ESP_OK;
}

void ppa_engine_deinit(void)
{
    if (s_ppa_fill_client) {
        ppa_unregister_client(s_ppa_fill_client);
        s_ppa_fill_client = NULL;
    }
    if (s_ppa_blend_client) {
        ppa_unregister_client(s_ppa_blend_client);
        s_ppa_blend_client = NULL;
    }
    if (s_ppa_srm_client) {
        ppa_unregister_client(s_ppa_srm_client);
        s_ppa_srm_client = NULL;
    }
    ESP_LOGI(TAG, "PPA 2D engine deinitialized");
}

/* ========================= FILL Operations ========================= */

esp_err_t ppa_fill_rect_rgb565(void *buf, size_t buf_size,
                                uint32_t pic_w, uint32_t pic_h,
                                uint32_t x, uint32_t y,
                                uint32_t fill_w, uint32_t fill_h,
                                uint16_t rgb565_color)
{
    if (!s_ppa_fill_client) return ESP_ERR_INVALID_STATE;

    /* Convert RGB565 to ARGB8888 components for the fill color */
    uint8_t r = ((rgb565_color >> 11) & 0x1F) << 3;
    uint8_t g = ((rgb565_color >> 5) & 0x3F) << 2;
    uint8_t b = (rgb565_color & 0x1F) << 3;

    ppa_fill_oper_config_t fill_cfg = {
        .out = {
            .buffer = buf,
            .buffer_size = buf_size,
            .pic_w = pic_w,
            .pic_h = pic_h,
            .block_offset_x = x,
            .block_offset_y = y,
            .fill_cm = PPA_FILL_COLOR_MODE_RGB565,
        },
        .fill_block_w = fill_w,
        .fill_block_h = fill_h,
        .fill_argb_color = {
            .a = 0xFF,
            .r = r,
            .g = g,
            .b = b,
        },
        .mode = PPA_TRANS_MODE_BLOCKING,
    };

    return ppa_do_fill(s_ppa_fill_client, &fill_cfg);
}

esp_err_t ppa_create_filled_buffer(uint32_t width, uint32_t height,
                                    uint16_t rgb565_color,
                                    void **out_buf, size_t *out_size)
{
    if (!out_buf || !out_size) return ESP_ERR_INVALID_ARG;

    size_t size = width * height * sizeof(uint16_t);
    size_t aligned_size = (size + s_buf_align - 1) & ~(s_buf_align - 1);
    void *buf = ppa_alloc_buf(aligned_size);
    if (!buf) return ESP_ERR_NO_MEM;

    esp_err_t ret = ppa_fill_rect_rgb565(buf, aligned_size,
                                          width, height,
                                          0, 0, width, height,
                                          rgb565_color);
    if (ret != ESP_OK) {
        heap_caps_free(buf);
        return ret;
    }

    *out_buf = buf;
    *out_size = aligned_size;
    return ESP_OK;
}

/* ========================= SRM Operations ========================= */

esp_err_t ppa_scale_rgb565(const void *in_buf, uint32_t in_w, uint32_t in_h,
                            uint32_t out_w, uint32_t out_h,
                            void **out_buf, size_t *out_size)
{
    if (!s_ppa_srm_client || !in_buf || !out_buf || !out_size) return ESP_ERR_INVALID_ARG;

    size_t size = out_w * out_h * sizeof(uint16_t);
    size_t aligned_size = (size + s_buf_align - 1) & ~(s_buf_align - 1);
    void *buf = ppa_alloc_buf(aligned_size);
    if (!buf) return ESP_ERR_NO_MEM;

    float scale_x = (float)out_w / (float)in_w;
    float scale_y = (float)out_h / (float)in_h;

    ppa_srm_oper_config_t srm_cfg = {
        .in = {
            .buffer = in_buf,
            .pic_w = in_w,
            .pic_h = in_h,
            .block_w = in_w,
            .block_h = in_h,
            .block_offset_x = 0,
            .block_offset_y = 0,
            .srm_cm = PPA_SRM_COLOR_MODE_RGB565,
        },
        .out = {
            .buffer = buf,
            .buffer_size = aligned_size,
            .pic_w = out_w,
            .pic_h = out_h,
            .block_offset_x = 0,
            .block_offset_y = 0,
            .srm_cm = PPA_SRM_COLOR_MODE_RGB565,
        },
        .rotation_angle = PPA_SRM_ROTATION_ANGLE_0,
        .scale_x = scale_x,
        .scale_y = scale_y,
        .mirror_x = false,
        .mirror_y = false,
        .rgb_swap = false,
        .byte_swap = false,
        .mode = PPA_TRANS_MODE_BLOCKING,
    };

    esp_err_t ret = ppa_do_scale_rotate_mirror(s_ppa_srm_client, &srm_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "PPA SRM scale failed: %s", esp_err_to_name(ret));
        heap_caps_free(buf);
        return ret;
    }

    ESP_LOGD(TAG, "PPA scaled %ux%u -> %ux%u (%.2fx, %.2fy)",
             (unsigned)in_w, (unsigned)in_h, (unsigned)out_w, (unsigned)out_h,
             scale_x, scale_y);

    *out_buf = buf;
    *out_size = aligned_size;
    return ESP_OK;
}

esp_err_t ppa_rotate_rgb565(const void *in_buf, uint32_t in_w, uint32_t in_h,
                             uint32_t angle_deg,
                             void **out_buf, size_t *out_size,
                             uint32_t *out_w, uint32_t *out_h)
{
    if (!s_ppa_srm_client || !in_buf || !out_buf || !out_size) return ESP_ERR_INVALID_ARG;

    ppa_srm_rotation_angle_t rotation;
    uint32_t result_w, result_h;

    switch (angle_deg) {
        case 0:
            rotation = PPA_SRM_ROTATION_ANGLE_0;
            result_w = in_w; result_h = in_h;
            break;
        case 90:
            rotation = PPA_SRM_ROTATION_ANGLE_90;
            result_w = in_h; result_h = in_w;
            break;
        case 180:
            rotation = PPA_SRM_ROTATION_ANGLE_180;
            result_w = in_w; result_h = in_h;
            break;
        case 270:
            rotation = PPA_SRM_ROTATION_ANGLE_270;
            result_w = in_h; result_h = in_w;
            break;
        default:
            ESP_LOGE(TAG, "Invalid rotation angle: %u (must be 0/90/180/270)", (unsigned)angle_deg);
            return ESP_ERR_INVALID_ARG;
    }

    size_t size = result_w * result_h * sizeof(uint16_t);
    size_t aligned_size = (size + s_buf_align - 1) & ~(s_buf_align - 1);
    void *buf = ppa_alloc_buf(aligned_size);
    if (!buf) return ESP_ERR_NO_MEM;

    ppa_srm_oper_config_t srm_cfg = {
        .in = {
            .buffer = in_buf,
            .pic_w = in_w,
            .pic_h = in_h,
            .block_w = in_w,
            .block_h = in_h,
            .block_offset_x = 0,
            .block_offset_y = 0,
            .srm_cm = PPA_SRM_COLOR_MODE_RGB565,
        },
        .out = {
            .buffer = buf,
            .buffer_size = aligned_size,
            .pic_w = result_w,
            .pic_h = result_h,
            .block_offset_x = 0,
            .block_offset_y = 0,
            .srm_cm = PPA_SRM_COLOR_MODE_RGB565,
        },
        .rotation_angle = rotation,
        .scale_x = 1.0f,
        .scale_y = 1.0f,
        .mirror_x = false,
        .mirror_y = false,
        .rgb_swap = false,
        .byte_swap = false,
        .mode = PPA_TRANS_MODE_BLOCKING,
    };

    esp_err_t ret = ppa_do_scale_rotate_mirror(s_ppa_srm_client, &srm_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "PPA SRM rotate failed: %s", esp_err_to_name(ret));
        heap_caps_free(buf);
        return ret;
    }

    ESP_LOGD(TAG, "PPA rotated %ux%u by %u° -> %ux%u",
             (unsigned)in_w, (unsigned)in_h, (unsigned)angle_deg,
             (unsigned)result_w, (unsigned)result_h);

    *out_buf = buf;
    *out_size = aligned_size;
    if (out_w) *out_w = result_w;
    if (out_h) *out_h = result_h;
    return ESP_OK;
}

esp_err_t ppa_mirror_rgb565(const void *in_buf, uint32_t in_w, uint32_t in_h,
                             bool mirror_x, bool mirror_y,
                             void **out_buf, size_t *out_size)
{
    if (!s_ppa_srm_client || !in_buf || !out_buf || !out_size) return ESP_ERR_INVALID_ARG;

    size_t size = in_w * in_h * sizeof(uint16_t);
    size_t aligned_size = (size + s_buf_align - 1) & ~(s_buf_align - 1);
    void *buf = ppa_alloc_buf(aligned_size);
    if (!buf) return ESP_ERR_NO_MEM;

    ppa_srm_oper_config_t srm_cfg = {
        .in = {
            .buffer = in_buf,
            .pic_w = in_w,
            .pic_h = in_h,
            .block_w = in_w,
            .block_h = in_h,
            .block_offset_x = 0,
            .block_offset_y = 0,
            .srm_cm = PPA_SRM_COLOR_MODE_RGB565,
        },
        .out = {
            .buffer = buf,
            .buffer_size = aligned_size,
            .pic_w = in_w,
            .pic_h = in_h,
            .block_offset_x = 0,
            .block_offset_y = 0,
            .srm_cm = PPA_SRM_COLOR_MODE_RGB565,
        },
        .rotation_angle = PPA_SRM_ROTATION_ANGLE_0,
        .scale_x = 1.0f,
        .scale_y = 1.0f,
        .mirror_x = mirror_x,
        .mirror_y = mirror_y,
        .rgb_swap = false,
        .byte_swap = false,
        .mode = PPA_TRANS_MODE_BLOCKING,
    };

    esp_err_t ret = ppa_do_scale_rotate_mirror(s_ppa_srm_client, &srm_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "PPA SRM mirror failed: %s", esp_err_to_name(ret));
        heap_caps_free(buf);
        return ret;
    }

    ESP_LOGI(TAG, "PPA mirrored %ux%u (x=%d, y=%d)",
             (unsigned)in_w, (unsigned)in_h, mirror_x, mirror_y);

    *out_buf = buf;
    *out_size = aligned_size;
    return ESP_OK;
}

esp_err_t ppa_rgb_swap_rgb565(const void *in_buf, uint32_t in_w, uint32_t in_h,
                               void **out_buf, size_t *out_size)
{
    if (!s_ppa_srm_client || !in_buf || !out_buf || !out_size) return ESP_ERR_INVALID_ARG;

    size_t size = in_w * in_h * sizeof(uint16_t);
    size_t aligned_size = (size + s_buf_align - 1) & ~(s_buf_align - 1);
    void *buf = ppa_alloc_buf(aligned_size);
    if (!buf) return ESP_ERR_NO_MEM;

    ppa_srm_oper_config_t srm_cfg = {
        .in = {
            .buffer = in_buf,
            .pic_w = in_w,
            .pic_h = in_h,
            .block_w = in_w,
            .block_h = in_h,
            .block_offset_x = 0,
            .block_offset_y = 0,
            .srm_cm = PPA_SRM_COLOR_MODE_RGB565,
        },
        .out = {
            .buffer = buf,
            .buffer_size = aligned_size,
            .pic_w = in_w,
            .pic_h = in_h,
            .block_offset_x = 0,
            .block_offset_y = 0,
            .srm_cm = PPA_SRM_COLOR_MODE_RGB565,
        },
        .rotation_angle = PPA_SRM_ROTATION_ANGLE_0,
        .scale_x = 1.0f,
        .scale_y = 1.0f,
        .mirror_x = false,
        .mirror_y = false,
        .rgb_swap = true,   /* This swaps RGB channels in hardware */
        .byte_swap = false,
        .mode = PPA_TRANS_MODE_BLOCKING,
    };

    esp_err_t ret = ppa_do_scale_rotate_mirror(s_ppa_srm_client, &srm_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "PPA SRM rgb_swap failed: %s", esp_err_to_name(ret));
        heap_caps_free(buf);
        return ret;
    }

    ESP_LOGI(TAG, "PPA RGB swap completed %ux%u", (unsigned)in_w, (unsigned)in_h);

    *out_buf = buf;
    *out_size = aligned_size;
    return ESP_OK;
}

/* ========================= BLEND Operations ========================= */

esp_err_t ppa_blend_rgb565(const void *bg_buf, uint32_t bg_w, uint32_t bg_h,
                            const void *fg_buf, uint32_t fg_w, uint32_t fg_h,
                            uint8_t fg_alpha,
                            void **out_buf, size_t *out_size)
{
    if (!s_ppa_blend_client || !bg_buf || !fg_buf || !out_buf || !out_size)
        return ESP_ERR_INVALID_ARG;

    /* Blend dimensions must match — use the minimum */
    uint32_t w = (bg_w < fg_w) ? bg_w : fg_w;
    uint32_t h = (bg_h < fg_h) ? bg_h : fg_h;

    size_t size = w * h * sizeof(uint16_t);
    size_t aligned_size = (size + s_buf_align - 1) & ~(s_buf_align - 1);
    void *buf = ppa_alloc_buf(aligned_size);
    if (!buf) return ESP_ERR_NO_MEM;

    ppa_blend_oper_config_t blend_cfg = {
        .in_bg = {
            .buffer = bg_buf,
            .pic_w = bg_w,
            .pic_h = bg_h,
            .block_w = w,
            .block_h = h,
            .block_offset_x = 0,
            .block_offset_y = 0,
            .blend_cm = PPA_BLEND_COLOR_MODE_RGB565,
        },
        .in_fg = {
            .buffer = fg_buf,
            .pic_w = fg_w,
            .pic_h = fg_h,
            .block_w = w,
            .block_h = h,
            .block_offset_x = 0,
            .block_offset_y = 0,
            .blend_cm = PPA_BLEND_COLOR_MODE_RGB565,
        },
        .out = {
            .buffer = buf,
            .buffer_size = aligned_size,
            .pic_w = w,
            .pic_h = h,
            .block_offset_x = 0,
            .block_offset_y = 0,
            .blend_cm = PPA_BLEND_COLOR_MODE_RGB565,
        },
        .bg_alpha_update_mode = PPA_ALPHA_NO_CHANGE,
        .fg_alpha_update_mode = PPA_ALPHA_FIX_VALUE,
        .fg_alpha_fix_val = fg_alpha,
        .bg_ck_en = false,
        .fg_ck_en = false,
        .mode = PPA_TRANS_MODE_BLOCKING,
    };

    esp_err_t ret = ppa_do_blend(s_ppa_blend_client, &blend_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "PPA Blend failed: %s", esp_err_to_name(ret));
        heap_caps_free(buf);
        return ret;
    }

    ESP_LOGI(TAG, "PPA blended %ux%u (fg alpha=%u)", (unsigned)w, (unsigned)h, fg_alpha);

    *out_buf = buf;
    *out_size = aligned_size;
    return ESP_OK;
}

/* =================== Combined Rotate+Scale (in-place) =================== */

esp_err_t ppa_rotate_scale_rgb565_to(const void *in_buf, uint32_t in_w, uint32_t in_h,
                                      uint32_t angle_deg,
                                      float scale_x, float scale_y,
                                      void *out_buf, size_t out_buf_size,
                                      uint32_t *out_w, uint32_t *out_h,
                                      bool byte_swap)
{
    if (!s_ppa_srm_client || !in_buf || !out_buf) return ESP_ERR_INVALID_ARG;

    ppa_srm_rotation_angle_t rotation;

    switch (angle_deg) {
        case 0:   rotation = PPA_SRM_ROTATION_ANGLE_0;   break;
        case 90:  rotation = PPA_SRM_ROTATION_ANGLE_90;  break;
        case 180: rotation = PPA_SRM_ROTATION_ANGLE_180; break;
        case 270: rotation = PPA_SRM_ROTATION_ANGLE_270; break;
        default:
            ESP_LOGE(TAG, "Invalid rotation angle: %u", (unsigned)angle_deg);
            return ESP_ERR_INVALID_ARG;
    }

    /*
     * PPA hardware does Scale→Rotate→Mirror.  scale_x/scale_y from the
     * caller are specified as *post-rotation* factors.  For 90°/270°
     * rotations the axes swap, so we must feed swapped factors to the
     * hardware so the final (post-rotate) dimensions come out correct.
     */
    float ppa_sx = scale_x, ppa_sy = scale_y;
    if (angle_deg == 90 || angle_deg == 270) {
        ppa_sx = scale_y;   /* input-X scale → becomes output-Y after rot */
        ppa_sy = scale_x;   /* input-Y scale → becomes output-X after rot */
    }

    /* Compute final output dimensions (after S→R) */
    uint32_t scaled_w = (uint32_t)(in_w * ppa_sx);
    uint32_t scaled_h = (uint32_t)(in_h * ppa_sy);
    uint32_t result_w, result_h;
    if (angle_deg == 90 || angle_deg == 270) {
        result_w = scaled_h;
        result_h = scaled_w;
    } else {
        result_w = scaled_w;
        result_h = scaled_h;
    }

    size_t needed = result_w * result_h * sizeof(uint16_t);
    if (out_buf_size < needed) {
        ESP_LOGE(TAG, "Output buffer too small: %u < %u", (unsigned)out_buf_size, (unsigned)needed);
        return ESP_ERR_INVALID_SIZE;
    }

    ppa_srm_oper_config_t srm_cfg = {
        .in = {
            .buffer = in_buf,
            .pic_w = in_w,
            .pic_h = in_h,
            .block_w = in_w,
            .block_h = in_h,
            .block_offset_x = 0,
            .block_offset_y = 0,
            .srm_cm = PPA_SRM_COLOR_MODE_RGB565,
        },
        .out = {
            .buffer = out_buf,
            .buffer_size = out_buf_size,
            .pic_w = result_w,
            .pic_h = result_h,
            .block_offset_x = 0,
            .block_offset_y = 0,
            .srm_cm = PPA_SRM_COLOR_MODE_RGB565,
        },
        .rotation_angle = rotation,
        .scale_x = ppa_sx,
        .scale_y = ppa_sy,
        .mirror_x = false,
        .mirror_y = false,
        .rgb_swap = false,
        .byte_swap = byte_swap,
        .mode = PPA_TRANS_MODE_BLOCKING,
    };

    esp_err_t ret = ppa_do_scale_rotate_mirror(s_ppa_srm_client, &srm_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "PPA SRM rotate+scale failed: %s", esp_err_to_name(ret));
        return ret;
    }

    if (out_w) *out_w = result_w;
    if (out_h) *out_h = result_h;
    return ESP_OK;
}
