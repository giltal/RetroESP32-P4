/*
 * GT911 Touch component - public header
 * Ported from Arduino C++ to ESP-IDF C
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the GT911 touch controller
 *
 * @param sda_pin I2C SDA GPIO
 * @param scl_pin I2C SCL GPIO
 * @param rst_pin Reset GPIO (-1 if unused)
 * @param int_pin Interrupt GPIO (-1 if unused)
 * @return ESP_OK on success
 */
esp_err_t gt911_touch_init(int8_t sda_pin, int8_t scl_pin, int8_t rst_pin, int8_t int_pin);

/**
 * @brief Read touch coordinates
 *
 * @param x Pointer to X coordinate
 * @param y Pointer to Y coordinate
 * @return true if touched, false otherwise
 */
bool gt911_touch_get_xy(uint16_t *x, uint16_t *y);

#ifdef __cplusplus
}
#endif
