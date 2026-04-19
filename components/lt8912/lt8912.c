/*
 * LT8912B MIPI DSI to HDMI Bridge Driver
 *
 * Register sequences matched to the OLIMEX ESP32-P4-PC BSP proven implementation.
 *
 * I2C register banks:
 *   0x48 (MAIN)    — Digital clock, TX analog, CBUS, HDMI PLL, MIPI analog, LVDS
 *   0x49 (CEC/DSI) — MIPI lane config, video timing, DDS PLL
 *   0x4A (AVI)     — AVI InfoFrame, Audio IIS
 */

#include "lt8912.h"
#include "esp_log.h"
#include "esp_check.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "lt8912";

/* ---------- Low-level I2C helpers ---------- */

static esp_err_t reg_write(i2c_master_dev_handle_t dev, uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = {reg, val};
    return i2c_master_transmit(dev, buf, 2, 100);
}

static esp_err_t reg_read(i2c_master_dev_handle_t dev, uint8_t reg, uint8_t *val)
{
    return i2c_master_transmit_receive(dev, &reg, 1, val, 1, 100);
}

typedef struct {
    uint8_t reg;
    uint8_t val;
} reg_val_t;

static esp_err_t reg_write_seq(i2c_master_dev_handle_t dev, const reg_val_t *seq, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        esp_err_t ret = reg_write(dev, seq[i].reg, seq[i].val);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "I2C write failed: reg=0x%02X val=0x%02X err=%s",
                     seq[i].reg, seq[i].val, esp_err_to_name(ret));
            return ret;
        }
    }
    return ESP_OK;
}

#define WRITE_SEQ(handle, arr) reg_write_seq((handle), (arr), sizeof(arr) / sizeof(arr[0]))

/* ================================================================
 * Register tables — matched exactly to OLIMEX BSP esp_lcd_lt8912b.c
 * ================================================================ */

/* Digital Clock Enable — MAIN (0x48) — matched to BSP exactly */
static const reg_val_t cmd_digital_clock_en[] = {
    {0x02, 0xF7},   /* LVDS PLL held in reset (BSP has this, kernel doesn't) */
    {0x08, 0xFF},
    {0x09, 0xFF},
    {0x0A, 0xFF},
    {0x0B, 0x7C},
    {0x0C, 0xFF},
};

/* TX Analog — MAIN (0x48) — BSP proven values */
static const reg_val_t cmd_tx_analog[] = {
    {0x31, 0xE1},   /* BSP: 0xE1 — HDMI TX drive strength */
    {0x32, 0xE1},   /* BSP: 0xE1 */
    {0x33, 0x0C},   /* BSP: HDMI off initially (enabled later at end) */
    {0x37, 0x00},
    {0x38, 0x22},
    {0x60, 0x82},
};

/* CBUS Analog — MAIN (0x48) */
static const reg_val_t cmd_cbus_analog[] = {
    {0x39, 0x45},
    {0x3A, 0x00},
    {0x3B, 0x00},
};

/* HDMI PLL Analog — MAIN (0x48) */
static const reg_val_t cmd_hdmi_pll_analog[] = {
    {0x44, 0x31},
    {0x55, 0x44},
    {0x57, 0x01},
    {0x5A, 0x02},
};

/* Audio IIS Mode — MAIN (0x48) */
static const reg_val_t cmd_audio_iis_mode[] = {
    {0xB2, 0x01},   /* HDMI mode (not DVI) */
};

/* Audio IIS Enable — AVI (0x4A) */
static const reg_val_t cmd_audio_iis_en[] = {
    {0x06, 0x08},
    {0x07, 0xF0},
    {0x34, 0xD2},
    {0x0F, 0x2B},
};

/* LVDS Bypass — MAIN (0x48) — matched to OLIMEX BSP exactly (14 entries) */
static const reg_val_t cmd_lvds[] = {
    {0x44, 0x30},
    {0x51, 0x05},
    {0x50, 0x24},
    {0x51, 0x2D},
    {0x52, 0x04},
    {0x69, 0x0E},
    {0x69, 0x8E},
    {0x6A, 0x00},
    {0x6C, 0xB8},
    {0x6B, 0x51},
    {0x04, 0xFB},   /* core PLL reset */
    {0x04, 0xFF},   /* core PLL release */
    {0x7F, 0x00},   /* disable scaler */
    {0xA8, 0x13},   /* VSEA mode */
};

/* DDS Config — CEC/DSI (0x49)
 * Kernel upstream defaults. 0x51 starts at 0x80, ends at 0x00 (MIPI input mode).
 * Phase accumulator values (0x4E-0x50) only matter when 0x51=0x80 (internal PLL). */
static const reg_val_t cmd_dds_config[] = {
    {0x4E, 0x93},   /* DDS phase LSB (OLIMEX BSP value for 40MHz) */
    {0x4F, 0x3E},   /* DDS phase MID */
    {0x50, 0x29},   /* DDS phase MSB */
    {0x51, 0x80},
    {0x1E, 0x4F},
    {0x1F, 0x5E},
    {0x20, 0x01},
    {0x21, 0x2C},
    {0x22, 0x01},
    {0x23, 0xFA},
    {0x24, 0x00},
    {0x25, 0xC8},
    {0x26, 0x00},
    {0x27, 0x5E},
    {0x28, 0x01},
    {0x29, 0x2C},
    {0x2A, 0x01},
    {0x2B, 0xFA},
    {0x2C, 0x00},
    {0x2D, 0xC8},
    {0x2E, 0x00},
    {0x42, 0x64},
    {0x43, 0x00},
    {0x44, 0x04},
    {0x45, 0x00},
    {0x46, 0x59},
    {0x47, 0x00},
    {0x48, 0xF2},
    {0x49, 0x06},
    {0x4A, 0x00},
    {0x4B, 0x72},
    {0x4C, 0x45},
    {0x4D, 0x00},
    {0x52, 0x08},
    {0x53, 0x00},
    {0x54, 0xB2},
    {0x55, 0x00},
    {0x56, 0xE4},
    {0x57, 0x0D},
    {0x58, 0x00},
    {0x59, 0xE4},
    {0x5A, 0x8A},
    {0x5B, 0x00},
    {0x5C, 0x34},
    {0x51, 0x00},
};

/* ---------- Internal functions ---------- */

static esp_err_t lt8912_write_mipi_analog(lt8912_dev_t *dev)
{
    esp_err_t ret = ESP_OK;
    ret |= reg_write(dev->i2c_main, 0x3E, 0xD6);   /* P/N no swap */
    ret |= reg_write(dev->i2c_main, 0x3F, 0xD4);   /* EQ */
    ret |= reg_write(dev->i2c_main, 0x41, 0x3C);
    return ret;
}

static esp_err_t lt8912_write_mipi_basic_set(lt8912_dev_t *dev)
{
    esp_err_t ret = ESP_OK;
    ret |= reg_write(dev->i2c_cec, 0x10, 0x01);   /* term en */
    ret |= reg_write(dev->i2c_cec, 0x11, 0x10);   /* settle = 0x10 (BSP proven) */
    /* reg 0x12 (trail) intentionally NOT written — OLIMEX BSP commented it out */
    ret |= reg_write(dev->i2c_cec, 0x13, dev->mipi_lanes);  /* lane count */
    ret |= reg_write(dev->i2c_cec, 0x14, 0x00);   /* debug mux */
    ret |= reg_write(dev->i2c_cec, 0x15, 0x00);   /* no lane swap */
    ret |= reg_write(dev->i2c_cec, 0x1A, 0x03);   /* hshift */
    ret |= reg_write(dev->i2c_cec, 0x1B, 0x03);   /* vshift */
    return ret;
}

static esp_err_t lt8912_write_video_setup(lt8912_dev_t *dev)
{
    const lt8912_video_timing_t *t = &dev->timing;
    uint16_t h_total = t->h_active + t->h_front_porch + t->h_sync_width + t->h_back_porch;
    uint16_t v_total = t->v_active + t->v_front_porch + t->v_sync_width + t->v_back_porch;

    ESP_LOGI(TAG, "Video timing: %dx%d htotal=%d vtotal=%d pclk=%lu MHz",
             t->h_active, t->v_active, h_total, v_total, t->pclk_mhz);

    esp_err_t ret = ESP_OK;
    i2c_master_dev_handle_t cec = dev->i2c_cec;

    ret |= reg_write(cec, 0x18, (uint8_t)(t->h_sync_width % 256));
    ret |= reg_write(cec, 0x19, (uint8_t)(t->v_sync_width % 256));
    ret |= reg_write(cec, 0x1C, (uint8_t)(t->h_active % 256));
    ret |= reg_write(cec, 0x1D, (uint8_t)(t->h_active / 256));
    ret |= reg_write(cec, 0x2F, 0x0C);   /* fifo_buff_length = 12 */
    ret |= reg_write(cec, 0x34, (uint8_t)(h_total % 256));
    ret |= reg_write(cec, 0x35, (uint8_t)(h_total / 256));
    ret |= reg_write(cec, 0x36, (uint8_t)(v_total % 256));
    ret |= reg_write(cec, 0x37, (uint8_t)(v_total / 256));
    ret |= reg_write(cec, 0x38, (uint8_t)(t->v_back_porch % 256));
    ret |= reg_write(cec, 0x39, (uint8_t)(t->v_back_porch / 256));
    ret |= reg_write(cec, 0x3A, (uint8_t)(t->v_front_porch % 256));
    ret |= reg_write(cec, 0x3B, (uint8_t)(t->v_front_porch / 256));
    ret |= reg_write(cec, 0x3C, (uint8_t)(t->h_back_porch % 256));
    ret |= reg_write(cec, 0x3D, (uint8_t)(t->h_back_porch / 256));
    ret |= reg_write(cec, 0x3E, (uint8_t)(t->h_front_porch % 256));
    ret |= reg_write(cec, 0x3F, (uint8_t)(t->h_front_porch / 256));

    return ret;
}

static esp_err_t lt8912_write_avi_infoframe(lt8912_dev_t *dev)
{
    const lt8912_video_timing_t *t = &dev->timing;
    uint8_t vic = t->vic;
    uint8_t ar  = t->aspect_ratio;
    uint8_t pb2 = (ar << 4) + 0x08;
    uint8_t pb4 = vic;
    uint8_t pb0 = (((pb2 + pb4) <= 0x5F) ? (0x5F - pb2 - pb4) : (0x15F - pb2 - pb4));
    uint8_t sync_polarity = (t->h_polarity * 0x02) + (t->v_polarity * 0x01);

    esp_err_t ret = ESP_OK;

    /* Enable null package — AVI bank */
    ret |= reg_write(dev->i2c_avi, 0x3C, 0x41);

    /* Sync polarity — MAIN bank */
    ret |= reg_write(dev->i2c_main, 0xAB, sync_polarity);

    /* AVI InfoFrame data — AVI bank */
    ret |= reg_write(dev->i2c_avi, 0x43, pb0);    /* PB0: checksum */
    ret |= reg_write(dev->i2c_avi, 0x44, 0x10);   /* PB1: RGB888 */
    ret |= reg_write(dev->i2c_avi, 0x45, pb2);    /* PB2: aspect ratio */
    ret |= reg_write(dev->i2c_avi, 0x46, 0x00);   /* PB3 */
    ret |= reg_write(dev->i2c_avi, 0x47, pb4);    /* PB4: VIC */

    ESP_LOGI(TAG, "AVI InfoFrame: VIC=%d AR=%d sync_pol=0x%02X", vic, ar, sync_polarity);
    return ret;
}

static esp_err_t lt8912_detect_input_mipi(lt8912_dev_t *dev)
{
    uint8_t hsync_l = 0, hsync_h = 0, vsync_l = 0, vsync_h = 0;
    reg_read(dev->i2c_main, 0x9C, &hsync_l);
    reg_read(dev->i2c_main, 0x9D, &hsync_h);
    reg_read(dev->i2c_main, 0x9E, &vsync_l);
    reg_read(dev->i2c_main, 0x9F, &vsync_h);
    ESP_LOGI(TAG, "MIPI input detect: H=0x%02X%02X V=0x%02X%02X", hsync_h, hsync_l, vsync_h, vsync_l);
    return ESP_OK;
}

esp_err_t lt8912_rx_logic_reset(lt8912_dev_t *dev)
{
    esp_err_t ret = ESP_OK;
    /* MIPI RX reset */
    ret |= reg_write(dev->i2c_main, 0x03, 0x7F);
    vTaskDelay(pdMS_TO_TICKS(10));
    ret |= reg_write(dev->i2c_main, 0x03, 0xFF);
    /* DDS reset (BSP does this) */
    ret |= reg_write(dev->i2c_main, 0x05, 0xFB);
    vTaskDelay(pdMS_TO_TICKS(10));
    ret |= reg_write(dev->i2c_main, 0x05, 0xFF);
    return ret;
}

static esp_err_t lt8912_hw_reset(lt8912_dev_t *dev)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << dev->reset_gpio),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&io_conf), TAG, "GPIO config failed");

    gpio_set_level(dev->reset_gpio, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(dev->reset_gpio, 1);
    vTaskDelay(pdMS_TO_TICKS(10));

    ESP_LOGI(TAG, "Hardware reset complete (GPIO %d)", dev->reset_gpio);
    return ESP_OK;
}

/* ==================== Public API ==================== */

esp_err_t lt8912_init(lt8912_dev_t *dev, const lt8912_config_t *config)
{
    dev->reset_gpio = config->reset_gpio;
    dev->mipi_lanes = config->mipi_lanes;
    dev->timing = config->timing;

    /* Add I2C devices for all three register banks */
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .scl_speed_hz = 100000,
    };

    dev_cfg.device_address = LT8912_I2C_ADDR_MAIN;
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(config->i2c_bus, &dev_cfg, &dev->i2c_main),
        TAG, "Add I2C 0x%02X failed", LT8912_I2C_ADDR_MAIN);

    dev_cfg.device_address = LT8912_I2C_ADDR_CEC_DSI;
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(config->i2c_bus, &dev_cfg, &dev->i2c_cec),
        TAG, "Add I2C 0x%02X failed", LT8912_I2C_ADDR_CEC_DSI);

    dev_cfg.device_address = LT8912_I2C_ADDR_AVI;
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(config->i2c_bus, &dev_cfg, &dev->i2c_avi),
        TAG, "Add I2C 0x%02X failed", LT8912_I2C_ADDR_AVI);

    /* Hardware reset */
    ESP_RETURN_ON_ERROR(lt8912_hw_reset(dev), TAG, "HW reset failed");

    /* Verify chip presence */
    ESP_LOGI(TAG, "Scanning I2C for LT8912...");
    esp_err_t probe_main = i2c_master_probe(config->i2c_bus, LT8912_I2C_ADDR_MAIN, 100);
    esp_err_t probe_cec  = i2c_master_probe(config->i2c_bus, LT8912_I2C_ADDR_CEC_DSI, 100);
    esp_err_t probe_avi  = i2c_master_probe(config->i2c_bus, LT8912_I2C_ADDR_AVI, 100);
    ESP_LOGI(TAG, "  0x%02X (MAIN):    %s", LT8912_I2C_ADDR_MAIN,    probe_main == ESP_OK ? "FOUND" : "NOT FOUND");
    ESP_LOGI(TAG, "  0x%02X (CEC/DSI): %s", LT8912_I2C_ADDR_CEC_DSI, probe_cec  == ESP_OK ? "FOUND" : "NOT FOUND");
    ESP_LOGI(TAG, "  0x%02X (AVI):     %s", LT8912_I2C_ADDR_AVI,     probe_avi  == ESP_OK ? "FOUND" : "NOT FOUND");

    if (probe_main != ESP_OK) {
        ESP_LOGW(TAG, "LT8912 not found! Scanning full I2C bus...");
        for (uint8_t addr = 0x08; addr < 0x78; addr++) {
            if (i2c_master_probe(config->i2c_bus, addr, 50) == ESP_OK) {
                ESP_LOGI(TAG, "  Found device at 0x%02X", addr);
            }
        }
        return ESP_ERR_NOT_FOUND;
    }

    /* Digital Clock Enable */
    ESP_RETURN_ON_ERROR(WRITE_SEQ(dev->i2c_main, cmd_digital_clock_en), TAG, "Digital clock failed");

    /* TX Analog */
    ESP_RETURN_ON_ERROR(WRITE_SEQ(dev->i2c_main, cmd_tx_analog), TAG, "TX analog failed");

    /* CBUS Analog */
    ESP_RETURN_ON_ERROR(WRITE_SEQ(dev->i2c_main, cmd_cbus_analog), TAG, "CBUS analog failed");

    /* HDMI PLL Analog */
    ESP_RETURN_ON_ERROR(WRITE_SEQ(dev->i2c_main, cmd_hdmi_pll_analog), TAG, "HDMI PLL failed");

    /* MIPI Analog */
    ESP_RETURN_ON_ERROR(lt8912_write_mipi_analog(dev), TAG, "MIPI analog failed");

    /* MIPI Basic Set */
    ESP_RETURN_ON_ERROR(lt8912_write_mipi_basic_set(dev), TAG, "MIPI basic set failed");

    ESP_LOGI(TAG, "LT8912 initialized (%d MIPI lanes)", dev->mipi_lanes);
    return ESP_OK;
}

esp_err_t lt8912_configure_video(lt8912_dev_t *dev)
{
    /* BSP proven order: DDS → setup → detect → setup → AVI → rxlogicres → audio → lvds → lvds_off → hdmi_on */

    /* 1. DDS Config */
    ESP_RETURN_ON_ERROR(WRITE_SEQ(dev->i2c_cec, cmd_dds_config), TAG, "DDS config failed");

    /* 2. Video timing setup — 1st pass */
    ESP_RETURN_ON_ERROR(lt8912_write_video_setup(dev), TAG, "Video setup failed");

    /* 3. MIPI input detection (diagnostic read) */
    lt8912_detect_input_mipi(dev);

    /* 4. Video timing setup — 2nd pass (BSP writes it twice) */
    ESP_RETURN_ON_ERROR(lt8912_write_video_setup(dev), TAG, "Video setup 2nd pass failed");

    /* 5. AVI InfoFrame */
    ESP_RETURN_ON_ERROR(lt8912_write_avi_infoframe(dev), TAG, "AVI InfoFrame failed");

    /* 6. RX Logic + DDS Reset */
    ESP_RETURN_ON_ERROR(lt8912_rx_logic_reset(dev), TAG, "RX logic reset failed");

    /* 7. Audio IIS Mode (HDMI) — MAIN bank */
    ESP_RETURN_ON_ERROR(WRITE_SEQ(dev->i2c_main, cmd_audio_iis_mode), TAG, "Audio mode failed");

    /* 8. Audio IIS Enable — AVI bank */
    ESP_RETURN_ON_ERROR(WRITE_SEQ(dev->i2c_avi, cmd_audio_iis_en), TAG, "Audio enable failed");

    /* 9. LVDS Bypass Config */
    ESP_RETURN_ON_ERROR(WRITE_SEQ(dev->i2c_main, cmd_lvds), TAG, "LVDS config failed");

    /* 10. LVDS Output OFF — BSP writes 0x44=0x31 after cmd_lvds (HDMI-only mode) */
    ESP_RETURN_ON_ERROR(reg_write(dev->i2c_main, 0x44, 0x31), TAG, "LVDS PLL off failed");

    /* 11. HDMI Output ON */
    ESP_RETURN_ON_ERROR(reg_write(dev->i2c_main, 0x33, 0x0E), TAG, "HDMI enable failed");
    ESP_LOGI(TAG, "HDMI output enabled");

    ESP_LOGI(TAG, "LT8912 video configured (%dx%d)", dev->timing.h_active, dev->timing.v_active);
    return ESP_OK;
}

esp_err_t lt8912_enable_test_pattern(lt8912_dev_t *dev)
{
    const lt8912_video_timing_t *t = &dev->timing;
    uint16_t h_total = t->h_active + t->h_front_porch + t->h_sync_width + t->h_back_porch;
    uint16_t v_total = t->v_active + t->v_front_porch + t->v_sync_width + t->v_back_porch;
    uint16_t de_dly = t->h_sync_width + t->h_back_porch;
    uint16_t de_top = t->v_sync_width + t->v_back_porch;

    ESP_LOGW(TAG, "Enabling LT8912 internal test pattern (bypasses MIPI)");
    ESP_LOGI(TAG, "  %dx%d htotal=%d vtotal=%d de_dly=%d de_top=%d pclk=%lu",
             t->h_active, t->v_active, h_total, v_total, de_dly, de_top, t->pclk_mhz);

    esp_err_t ret = ESP_OK;
    i2c_master_dev_handle_t cec = dev->i2c_cec;

    /* Pattern resolution set — CEC/DSI bank (0x49) */
    ret |= reg_write(cec, 0x72, 0x12);
    ret |= reg_write(cec, 0x73, (uint8_t)(de_dly % 256));         /* RGD_PTN_DE_DLY[7:0] */
    ret |= reg_write(cec, 0x74, (uint8_t)(de_dly / 256));         /* RGD_PTN_DE_DLY[11:8] */
    ret |= reg_write(cec, 0x75, (uint8_t)(de_top % 256));         /* RGD_PTN_DE_TOP[6:0] */
    ret |= reg_write(cec, 0x76, (uint8_t)(t->h_active % 256));    /* RGD_PTN_DE_CNT[7:0] */
    ret |= reg_write(cec, 0x77, (uint8_t)(t->v_active % 256));    /* RGD_PTN_DE_LIN[7:0] */
    ret |= reg_write(cec, 0x78, (uint8_t)(((t->v_active / 256) << 4) + (t->h_active / 256)));
    ret |= reg_write(cec, 0x79, (uint8_t)(h_total % 256));        /* RGD_PTN_H_TOTAL[7:0] */
    ret |= reg_write(cec, 0x7a, (uint8_t)(v_total % 256));        /* RGD_PTN_V_TOTAL[7:0] */
    ret |= reg_write(cec, 0x7b, (uint8_t)(((v_total / 256) << 4) + (h_total / 256)));
    ret |= reg_write(cec, 0x7c, (uint8_t)(t->h_sync_width % 256));  /* RGD_PTN_HWIDTH[7:0] */
    ret |= reg_write(cec, 0x7d, (uint8_t)(((t->h_sync_width / 256) << 6) + (t->v_sync_width % 64)));
    ret |= reg_write(cec, 0x70, 0x80);  /* pattern enable */
    ret |= reg_write(cec, 0x71, 0x51);
    ret |= reg_write(cec, 0x42, 0x12);

    /* h v d pol hdmi sel pll sel */
    ret |= reg_write(cec, 0x1e, 0x67);

    /* DDS pixel clock: DDS_initial_value = pclk_mhz * 0x16C16 */
    uint32_t dds = (uint32_t)(t->pclk_mhz * 0x16C16);
    ESP_LOGI(TAG, "  DDS_initial_value = 0x%06lX (pclk=%lu MHz)", dds, t->pclk_mhz);
    ret |= reg_write(cec, 0x4e, (uint8_t)(dds & 0xFF));
    ret |= reg_write(cec, 0x4f, (uint8_t)((dds >> 8) & 0xFF));
    ret |= reg_write(cec, 0x50, (uint8_t)((dds >> 16) & 0xFF));
    ret |= reg_write(cec, 0x51, 0x80);

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Test pattern write failed");
        return ret;
    }

    /* No resets after test pattern — BSP doesn't do them */
    ESP_LOGW(TAG, "Test pattern ENABLED — HDMI should show color bars (no MIPI needed)");
    return ESP_OK;
}

esp_err_t lt8912_write_pattern_reg(lt8912_dev_t *dev, uint8_t reg, uint8_t val)
{
    return reg_write(dev->i2c_cec, reg, val);
}

esp_err_t lt8912_write_main_reg(lt8912_dev_t *dev, uint8_t reg, uint8_t val)
{
    return reg_write(dev->i2c_main, reg, val);
}

esp_err_t lt8912_read_main_reg(lt8912_dev_t *dev, uint8_t reg, uint8_t *val)
{
    return reg_read(dev->i2c_main, reg, val);
}

esp_err_t lt8912_read_cec_reg(lt8912_dev_t *dev, uint8_t reg, uint8_t *val)
{
    return reg_read(dev->i2c_cec, reg, val);
}

esp_err_t lt8912_post_streaming_reset(lt8912_dev_t *dev)
{
    ESP_LOGI(TAG, "Post-streaming re-sync...");
    vTaskDelay(pdMS_TO_TICKS(100));

    /* Ensure DDS derives clock from MIPI (not internal PLL) */
    reg_write(dev->i2c_cec, 0x70, 0x00);  /* pattern off */
    reg_write(dev->i2c_cec, 0x51, 0x00);  /* MIPI clock source */
    reg_write(dev->i2c_cec, 0x1E, 0x4F);  /* PLL source for MIPI */

    /* RX Logic + DDS Reset */
    ESP_RETURN_ON_ERROR(lt8912_rx_logic_reset(dev), TAG, "RX logic reset failed");
    vTaskDelay(pdMS_TO_TICKS(50));

    /* Re-send video timing */
    ESP_RETURN_ON_ERROR(lt8912_write_video_setup(dev), TAG, "Video setup failed");

    /* Re-send AVI InfoFrame */
    ESP_RETURN_ON_ERROR(lt8912_write_avi_infoframe(dev), TAG, "AVI InfoFrame failed");

    /* Final RX Logic + DDS Reset */
    ESP_RETURN_ON_ERROR(lt8912_rx_logic_reset(dev), TAG, "RX logic reset 2 failed");

    /* MIPI input detection */
    lt8912_detect_input_mipi(dev);

    /* Read back key DDS registers for verification */
    uint8_t reg51, reg1e, reg70, reg02;
    reg_read(dev->i2c_cec, 0x51, &reg51);
    reg_read(dev->i2c_cec, 0x1E, &reg1e);
    reg_read(dev->i2c_cec, 0x70, &reg70);
    reg_read(dev->i2c_main, 0x02, &reg02);
    ESP_LOGI(TAG, "DDS verify: 0x51=0x%02X 0x1E=0x%02X 0x70=0x%02X 0x02=0x%02X", reg51, reg1e, reg70, reg02);

    ESP_LOGI(TAG, "Post-streaming re-sync complete");
    return ESP_OK;
}

bool lt8912_is_connected(lt8912_dev_t *dev)
{
    uint8_t reg_val = 0;
    esp_err_t ret = reg_read(dev->i2c_main, 0xC1, &reg_val);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "HPD read failed: %s", esp_err_to_name(ret));
        return false;
    }
    return (reg_val & 0x80) != 0;
}

void lt8912_dump_debug(lt8912_dev_t *dev)
{
    uint8_t val = 0, val2 = 0;
    ESP_LOGI(TAG, "=== LT8912 Debug Dump ===");

    reg_read(dev->i2c_main, 0x00, &val);
    reg_read(dev->i2c_main, 0x01, &val2);
    ESP_LOGI(TAG, "  Chip ID: 0x%02X%02X", val2, val);

    reg_read(dev->i2c_main, 0x02, &val);
    ESP_LOGI(TAG, "  [0x48] 0x02 (Clk/Reset):  0x%02X", val);
    reg_read(dev->i2c_main, 0x08, &val);
    ESP_LOGI(TAG, "  [0x48] 0x08 (Digi clk):   0x%02X", val);
    reg_read(dev->i2c_main, 0x33, &val);
    ESP_LOGI(TAG, "  [0x48] 0x33 (HDMI out):    0x%02X (0x0E=on)", val);
    reg_read(dev->i2c_main, 0xAB, &val);
    ESP_LOGI(TAG, "  [0x48] 0xAB (Sync pol):    0x%02X", val);
    reg_read(dev->i2c_main, 0xB2, &val);
    ESP_LOGI(TAG, "  [0x48] 0xB2 (HDMI/DVI):    0x%02X (bit0=HDMI)", val);
    reg_read(dev->i2c_main, 0xC1, &val);
    ESP_LOGI(TAG, "  [0x48] 0xC1 (HPD):         0x%02X (bit7=connected)", val);

    reg_read(dev->i2c_cec, 0x13, &val);
    ESP_LOGI(TAG, "  [0x49] 0x13 (Lanes):       0x%02X", val);
    reg_read(dev->i2c_cec, 0x1C, &val);
    reg_read(dev->i2c_cec, 0x1D, &val2);
    ESP_LOGI(TAG, "  [0x49] 0x1C-1D (H act):    %d", val | (val2 << 8));
    reg_read(dev->i2c_cec, 0x34, &val);
    reg_read(dev->i2c_cec, 0x35, &val2);
    ESP_LOGI(TAG, "  [0x49] 0x34-35 (H tot):    %d", val | (val2 << 8));

    uint8_t h_l = 0, h_h = 0, v_l = 0, v_h = 0;
    reg_read(dev->i2c_main, 0x9C, &h_l);
    reg_read(dev->i2c_main, 0x9D, &h_h);
    reg_read(dev->i2c_main, 0x9E, &v_l);
    reg_read(dev->i2c_main, 0x9F, &v_h);
    ESP_LOGI(TAG, "  [0x48] 0x9C-9F (MIPI det): H=0x%02X%02X V=0x%02X%02X", h_h, h_l, v_h, v_l);

    ESP_LOGI(TAG, "=========================");
}
