/*
 * USB HID Gamepad Host Component for ESP32-P4
 *
 * Uses the board's USB 2.0 Type-A host port to read wired USB gamepads.
 * Supports PS4 DualShock 4, PS5 DualSense, and generic HID gamepads.
 *
 * Architecture:
 *   - USB Host library task handles bus-level events
 *   - HID Host driver (managed component) handles HID class events
 *   - Gamepad task processes device connect/disconnect and input reports
 *   - Report parser auto-detects PS4/PS5/generic format
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ========================= Button Bitmask ========================= */

#define GAMEPAD_BTN_A           (1 << 0)    /**< Cross (PS) / A (Xbox) */
#define GAMEPAD_BTN_B           (1 << 1)    /**< Circle (PS) / B (Xbox) */
#define GAMEPAD_BTN_X           (1 << 2)    /**< Square (PS) / X (Xbox) */
#define GAMEPAD_BTN_Y           (1 << 3)    /**< Triangle (PS) / Y (Xbox) */
#define GAMEPAD_BTN_L1          (1 << 4)    /**< L1 / LB */
#define GAMEPAD_BTN_R1          (1 << 5)    /**< R1 / RB */
#define GAMEPAD_BTN_L2          (1 << 6)    /**< L2 digital / LT digital */
#define GAMEPAD_BTN_R2          (1 << 7)    /**< R2 digital / RT digital */
#define GAMEPAD_BTN_SELECT      (1 << 8)    /**< Share / Select / Create */
#define GAMEPAD_BTN_START       (1 << 9)    /**< Options / Start / Menu */
#define GAMEPAD_BTN_L3          (1 << 10)   /**< Left stick press */
#define GAMEPAD_BTN_R3          (1 << 11)   /**< Right stick press */
#define GAMEPAD_BTN_HOME        (1 << 12)   /**< PS / Guide / Home */
#define GAMEPAD_BTN_MISC        (1 << 13)   /**< Touchpad click / Misc */

/* ========================= D-Pad Values ========================= */

#define GAMEPAD_DPAD_UP         0x01
#define GAMEPAD_DPAD_DOWN       0x02
#define GAMEPAD_DPAD_LEFT       0x04
#define GAMEPAD_DPAD_RIGHT      0x08

/* ========================= Gamepad State ========================= */

/**
 * @brief Gamepad state — updated by the USB HID report parser
 */
typedef struct {
    uint8_t  connected;     /**< 0=disconnected, 1=connected */
    uint32_t buttons;       /**< Button bitmask (see GAMEPAD_BTN_*) */
    int16_t  axis_lx;       /**< Left stick X (-128..127) */
    int16_t  axis_ly;       /**< Left stick Y (-128..127) */
    int16_t  axis_rx;       /**< Right stick X (-128..127) */
    int16_t  axis_ry;       /**< Right stick Y (-128..127) */
    uint16_t brake;         /**< L2 trigger analog (0..1023) */
    uint16_t throttle;      /**< R2 trigger analog (0..1023) */
    uint8_t  dpad;          /**< D-pad bitmask (see GAMEPAD_DPAD_*) */
} gamepad_state_t;

/* ========================= Configuration ========================= */

/**
 * @brief Gamepad configuration (USB HID host)
 */
typedef struct {
    int usb_task_priority;   /**< USB host lib task priority (default: 2) */
    int hid_task_priority;   /**< HID host driver task priority (default: 5) */
    size_t usb_task_stack;   /**< USB host lib task stack size (default: 4096) */
    size_t hid_task_stack;   /**< HID host driver task stack size (default: 4096) */
} gamepad_config_t;

/**
 * @brief Default USB HID gamepad configuration
 */
#define GAMEPAD_CONFIG_DEFAULT() { \
    .usb_task_priority = 5, \
    .hid_task_priority = 6, \
    .usb_task_stack = 4096, \
    .hid_task_stack = 4096, \
}

/* ========================= API ========================= */

/**
 * @brief Initialize USB HID gamepad host
 *
 * Installs USB host library, HID host driver, and starts background tasks.
 * Gamepads plugged into the USB Type-A port will be detected automatically.
 *
 * @param config  Configuration (use GAMEPAD_CONFIG_DEFAULT() for defaults)
 * @return ESP_OK on success
 */
esp_err_t gamepad_init(const gamepad_config_t *config);

/**
 * @brief Deinitialize the gamepad host (stop tasks, uninstall drivers)
 */
void gamepad_deinit(void);

/**
 * @brief Get current gamepad state (thread-safe copy)
 * @param[out] state  Destination for the gamepad state snapshot
 */
void gamepad_get_state(gamepad_state_t *state);

/**
 * @brief Check if a gamepad is currently connected
 * @return true if connected
 */
bool gamepad_is_connected(void);

/**
 * @brief Convert a button bitmask to a human-readable string
 * @param buttons  Button bitmask
 * @param buf      Output buffer
 * @param buf_size Buffer size
 */
void gamepad_buttons_to_str(uint32_t buttons, char *buf, size_t buf_size);

/**
 * @brief Get the last raw HID report bytes (for debugging/mapping)
 * @param[out] buf       Output buffer (at least 64 bytes)
 * @param      buf_size  Size of output buffer
 * @return Number of bytes copied (0 if no report yet)
 */
int gamepad_get_raw_report(uint8_t *buf, size_t buf_size);

#ifdef __cplusplus
}
#endif
