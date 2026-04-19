#include "sdkconfig.h"
#ifdef CONFIG_HDMI_OUTPUT
#include <string.h>
#include "hdmi_display.h"
#include "lt8912.h"

#include "esp_log.h"
#include "esp_check.h"
#include "esp_ldo_regulator.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_io.h"
#include "driver/i2c_master.h"
#include "esp_cache.h"

static const char *TAG = "hdmi";

#define NUM_LANES    2
#define LANE_BITRATE 1000
#define PIXEL_CLK    40   /* MHz — minimum for P4 DSI bridge */
#define BPP          3    /* RGB888 */

typedef struct {
    uint16_t h_res, v_res;
    uint16_t h_fp, h_sync, h_bp;
    uint16_t v_fp, v_sync, v_bp;
    bool     h_pol, v_pol;
    uint8_t  vic, aspect;
} mode_timing_t;

static const mode_timing_t s_modes[] = {
    [HDMI_MODE_640x480] = {
        .h_res = 640,  .v_res = 480,
        .h_fp = 200, .h_sync = 96, .h_bp = 334,   /* htotal=1270 */
        .v_fp = 10,  .v_sync = 2,  .v_bp = 33,     /* vtotal=525  */
        .h_pol = false, .v_pol = false,              /* -H -V       */
        .vic = 0, .aspect = 1,                       /* 4:3         */
    },
    [HDMI_MODE_800x600] = {
        .h_res = 800,  .v_res = 600,
        .h_fp = 48, .h_sync = 128, .h_bp = 88,     /* htotal=1064 */
        .v_fp = 1,  .v_sync = 4,   .v_bp = 23,     /* vtotal=628  */
        .h_pol = true, .v_pol = true,                /* +H +V       */
        .vic = 0, .aspect = 2,                       /* 16:9        */
    },
};

esp_err_t hdmi_display_init(hdmi_mode_t mode, hdmi_display_t *out)
{
    if (mode > HDMI_MODE_800x600 || !out) return ESP_ERR_INVALID_ARG;
    const mode_timing_t *t = &s_modes[mode];

    uint32_t htotal = t->h_res + t->h_fp + t->h_sync + t->h_bp;
    uint32_t vtotal = t->v_res + t->v_fp + t->v_sync + t->v_bp;
    uint32_t fb_size = t->h_res * t->v_res * BPP;

    ESP_LOGI(TAG, "HDMI %dx%d @%luHz, pclk=%dMHz, FB=%luKB",
             t->h_res, t->v_res,
             (unsigned long)(PIXEL_CLK * 1000000UL / (htotal * vtotal)),
             PIXEL_CLK, (unsigned long)(fb_size / 1024));

    /* 1. LDO for MIPI PHY */
    esp_ldo_channel_handle_t ldo = NULL;
    esp_ldo_channel_config_t ldo_cfg = { .chan_id = 3, .voltage_mv = 2500 };
    ESP_RETURN_ON_ERROR(esp_ldo_acquire_channel(&ldo_cfg, &ldo), TAG, "LDO");

    /* 2. I2C bus */
    i2c_master_bus_config_t i2c_cfg = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = I2C_NUM_0,
        .scl_io_num = CONFIG_LT8912_I2C_SCL,
        .sda_io_num = CONFIG_LT8912_I2C_SDA,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    i2c_master_bus_handle_t i2c_bus;
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&i2c_cfg, &i2c_bus), TAG, "I2C");

    /* 3. DSI bus (PHY active, no streaming yet) */
    esp_lcd_dsi_bus_handle_t dsi_bus = NULL;
    esp_lcd_dsi_bus_config_t dsi_cfg = {
        .bus_id = 0,
        .num_data_lanes = NUM_LANES,
        .phy_clk_src = MIPI_DSI_PHY_CLK_SRC_DEFAULT,
        .lane_bit_rate_mbps = LANE_BITRATE,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_dsi_bus(&dsi_cfg, &dsi_bus), TAG, "DSI bus");

    /* 4. LT8912 init + configure video (before streaming) */
    lt8912_config_t lt_cfg = {
        .i2c_bus = i2c_bus,
        .reset_gpio = CONFIG_LT8912_RESET_GPIO,
        .mipi_lanes = NUM_LANES,
        .timing = {
            .h_active = t->h_res, .v_active = t->v_res,
            .h_front_porch = t->h_fp, .h_sync_width = t->h_sync, .h_back_porch = t->h_bp,
            .v_front_porch = t->v_fp, .v_sync_width = t->v_sync, .v_back_porch = t->v_bp,
            .h_polarity = t->h_pol, .v_polarity = t->v_pol,
            .vic = t->vic, .aspect_ratio = t->aspect, .pclk_mhz = PIXEL_CLK,
        },
    };
    static lt8912_dev_t lt_dev;
    ESP_RETURN_ON_ERROR(lt8912_init(&lt_dev, &lt_cfg), TAG, "LT8912 init");
    ESP_RETURN_ON_ERROR(lt8912_configure_video(&lt_dev), TAG, "LT8912 video");

    ESP_LOGI(TAG, "HPD: %s", lt8912_is_connected(&lt_dev) ? "CONNECTED" : "no");

    /* 5. DBI command I/O (required by IDF even if unused) */
    esp_lcd_panel_io_handle_t dbi_io = NULL;
    esp_lcd_dbi_io_config_t dbi_cfg = {
        .virtual_channel = 0, .lcd_cmd_bits = 8, .lcd_param_bits = 8,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_dbi(dsi_bus, &dbi_cfg, &dbi_io), TAG, "DBI");

    /* 6. DPI panel — RGB888, burst, disable_lp */
    esp_lcd_panel_handle_t panel = NULL;
    esp_lcd_dpi_panel_config_t dpi_cfg = {
        .dpi_clk_src = MIPI_DSI_DPI_CLK_SRC_DEFAULT,
        .dpi_clock_freq_mhz = PIXEL_CLK,
        .virtual_channel = 0,
        .pixel_format = LCD_COLOR_PIXEL_FORMAT_RGB888,
        .num_fbs = 1,
        .video_timing = {
            .h_size = t->h_res, .v_size = t->v_res,
            .hsync_back_porch = t->h_bp, .hsync_pulse_width = t->h_sync, .hsync_front_porch = t->h_fp,
            .vsync_back_porch = t->v_bp, .vsync_pulse_width = t->v_sync, .vsync_front_porch = t->v_fp,
        },
        .flags.disable_lp = true,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_dpi(dsi_bus, &dpi_cfg, &panel), TAG, "DPI panel");

    /* 7. Get framebuffer and clear to black */
    void *fb = NULL;
    ESP_RETURN_ON_ERROR(esp_lcd_dpi_panel_get_frame_buffer(panel, 1, &fb), TAG, "FB");
    memset(fb, 0, fb_size);
    ESP_RETURN_ON_ERROR(esp_cache_msync(fb, fb_size, ESP_CACHE_MSYNC_FLAG_DIR_C2M), TAG, "cache");

    /* 8. Start DPI streaming */
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(panel), TAG, "DPI start");
    ESP_LOGI(TAG, "HDMI %dx%d active — fb=%p (%lu bytes)", t->h_res, t->v_res, fb, (unsigned long)fb_size);

    /* Fill output */
    out->h_res = t->h_res;
    out->v_res = t->v_res;
    out->bpp = BPP;
    out->fb = fb;
    out->fb_size = fb_size;

    return ESP_OK;
}
#endif /* CONFIG_HDMI_OUTPUT */
