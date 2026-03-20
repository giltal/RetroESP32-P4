/*
 * SPDX-FileCopyrightText: 2023 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stdint.h>

#include "hal/lcd_types.h"
#include "esp_lcd_panel_vendor.h"

#if SOC_LCD_RGB_SUPPORTED
#include "esp_lcd_panel_rgb.h"
#endif

#if SOC_MIPI_DSI_SUPPORTED
#include "esp_lcd_mipi_dsi.h"
#endif

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief LCD panel initialization commands.
 */
typedef struct {
    int cmd;
    const void *data;
    size_t data_bytes;
    unsigned int delay_ms;
} st7701_lcd_init_cmd_t;

/**
 * @brief LCD panel vendor configuration.
 */
typedef struct {
    const st7701_lcd_init_cmd_t *init_cmds;
    uint16_t init_cmds_size;
    union {
#if SOC_LCD_RGB_SUPPORTED
        const esp_lcd_rgb_panel_config_t *rgb_config;
#endif
#if SOC_MIPI_DSI_SUPPORTED
        struct {
            esp_lcd_dsi_bus_handle_t dsi_bus;
            const esp_lcd_dpi_panel_config_t *dpi_config;
        } mipi_config;
#endif
    };
    struct {
        unsigned int use_mipi_interface: 1;
        unsigned int mirror_by_cmd: 1;
        union {
            unsigned int auto_del_panel_io: 1;
            unsigned int enable_io_multiplex: 1;
        };
    } flags;
} st7701_vendor_config_t;

/**
 * @brief Create LCD panel for model ST7701
 */
esp_err_t esp_lcd_new_panel_st7701(const esp_lcd_panel_io_handle_t io,
                                    const esp_lcd_panel_dev_config_t *panel_dev_config,
                                    esp_lcd_panel_handle_t *ret_panel);

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////// MIPI-DSI Configuration Macros /////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#define ST7701_PANEL_BUS_DSI_2CH_CONFIG()                \
    {                                                    \
        .bus_id = 0,                                     \
        .num_data_lanes = 2,                             \
        .phy_clk_src = MIPI_DSI_PHY_CLK_SRC_DEFAULT,    \
        .lane_bit_rate_mbps = 500,                       \
    }

#define ST7701_PANEL_IO_DBI_CONFIG()  \
    {                                 \
        .virtual_channel = 0,         \
        .lcd_cmd_bits = 8,            \
        .lcd_param_bits = 8,          \
    }

#define ST7701_480_800_PANEL_60HZ_DPI_CONFIG(px_format)  \
    {                                                    \
        .virtual_channel = 0,                            \
        .dpi_clk_src = MIPI_DSI_DPI_CLK_SRC_DEFAULT,    \
        .dpi_clock_freq_mhz = 34,                       \
        .pixel_format = px_format,                       \
        .num_fbs = 1,                                    \
        .video_timing = {                                \
            .h_size = 480,                               \
            .v_size = 800,                               \
            .hsync_pulse_width = 12,                     \
            .hsync_back_porch = 42,                      \
            .hsync_front_porch = 42,                     \
            .vsync_pulse_width = 2,                      \
            .vsync_back_porch = 8,                       \
            .vsync_front_porch = 166,                    \
        },                                               \
        .flags = {.use_dma2d = true,}                    \
    }

#ifdef __cplusplus
}
#endif
