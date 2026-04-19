#pragma once

#include "esp_err.h"
#include "driver/i2c_master.h"
#include "driver/gpio.h"

/*
 * LT8912B has three I2C register banks:
 *   0x48 (MAIN)    — Digital clock, TX analog, CBUS, HDMI PLL, MIPI analog, LVDS
 *   0x49 (CEC/DSI) — MIPI lane config, video timing, DDS PLL
 *   0x4A (AVI)     — AVI InfoFrame, Audio IIS
 */
#define LT8912_I2C_ADDR_MAIN     0x48
#define LT8912_I2C_ADDR_CEC_DSI  0x49
#define LT8912_I2C_ADDR_AVI      0x4A

typedef struct {
    uint16_t h_active;
    uint16_t v_active;
    uint16_t h_front_porch;
    uint16_t h_sync_width;
    uint16_t h_back_porch;
    uint16_t v_front_porch;
    uint16_t v_sync_width;
    uint16_t v_back_porch;
    bool     h_polarity;       // true = active high
    bool     v_polarity;       // true = active high
    uint16_t vic;              // Video Identification Code (0 if non-standard)
    uint8_t  aspect_ratio;     // 0=no data, 1=4:3, 2=16:9
    uint32_t pclk_mhz;        // Pixel clock in MHz
} lt8912_video_timing_t;

typedef struct {
    i2c_master_bus_handle_t i2c_bus;
    gpio_num_t reset_gpio;
    uint8_t mipi_lanes;
    lt8912_video_timing_t timing;
} lt8912_config_t;

typedef struct {
    i2c_master_dev_handle_t i2c_main;    // 0x48
    i2c_master_dev_handle_t i2c_cec;     // 0x49
    i2c_master_dev_handle_t i2c_avi;     // 0x4A
    gpio_num_t reset_gpio;
    uint8_t mipi_lanes;
    lt8912_video_timing_t timing;
} lt8912_dev_t;

/**
 * Initialize: HW reset, add I2C devices (0x48/0x49/0x4A), write analog/clock/MIPI basic config.
 */
esp_err_t lt8912_init(lt8912_dev_t *dev, const lt8912_config_t *config);

/**
 * Configure video output: DDS, timing, AVI InfoFrame, RX reset, LVDS, HDMI enable.
 * Call AFTER lt8912_init() and BEFORE starting the MIPI DSI DPI panel.
 */
esp_err_t lt8912_configure_video(lt8912_dev_t *dev);

/**
 * Re-sync LT8912 after MIPI DSI streaming has started.
 * Resets MIPI RX logic + DDS, re-sends video timing and AVI.
 */
esp_err_t lt8912_post_streaming_reset(lt8912_dev_t *dev);

/**
 * Enable LT8912 internal test pattern (bypasses MIPI entirely).
 * Call AFTER lt8912_configure_video(). No MIPI data needed.
 */
esp_err_t lt8912_enable_test_pattern(lt8912_dev_t *dev);

/**
 * Write a single register in the CEC/DSI bank (0x49) for pattern tweaking.
 */
esp_err_t lt8912_write_pattern_reg(lt8912_dev_t *dev, uint8_t reg, uint8_t val);

/**
 * Write a single register in the MAIN bank (0x48).
 */
esp_err_t lt8912_write_main_reg(lt8912_dev_t *dev, uint8_t reg, uint8_t val);

/**
 * Read a single register in the MAIN bank (0x48).
 */
esp_err_t lt8912_read_main_reg(lt8912_dev_t *dev, uint8_t reg, uint8_t *val);

/**
 * Read a single register in the CEC/DSI bank (0x49).
 */
esp_err_t lt8912_read_cec_reg(lt8912_dev_t *dev, uint8_t reg, uint8_t *val);

void lt8912_dump_debug(lt8912_dev_t *dev);
esp_err_t lt8912_rx_logic_reset(lt8912_dev_t *dev);
bool lt8912_is_connected(lt8912_dev_t *dev);
