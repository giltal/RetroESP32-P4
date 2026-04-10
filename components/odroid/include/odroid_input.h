/*
 * Odroid Input Compatibility Layer for ESP32-P4
 *
 * Maps USB HID gamepad to the original Odroid Go button layout.
 * Button mapping (native SNES controller):
 *   D-pad  → D-pad (UP/DOWN/LEFT/RIGHT)
 *   A      → A (right face)
 *   B      → B (bottom face)
 *   X      → X (top face)
 *   Y      → Y (left face)
 *   L1/L2  → L (left shoulder)
 *   R1/R2  → R (right shoulder)
 *   SELECT → SELECT
 *   START  → START
 *   Touch left  shoulder → MENU
 *   Touch right shoulder → VOLUME
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Button indices */
enum {
    ODROID_INPUT_UP = 0,
    ODROID_INPUT_RIGHT,
    ODROID_INPUT_DOWN,
    ODROID_INPUT_LEFT,
    ODROID_INPUT_SELECT,
    ODROID_INPUT_START,
    ODROID_INPUT_A,
    ODROID_INPUT_B,
    ODROID_INPUT_X,         /**< SNES X (top face button) */
    ODROID_INPUT_Y,         /**< SNES Y (left face button) */
    ODROID_INPUT_L,         /**< SNES L shoulder (L1/L2) */
    ODROID_INPUT_R,         /**< SNES R shoulder (R1/R2) */
    ODROID_INPUT_MENU,      /**< Virtual — touch left shoulder zone */
    ODROID_INPUT_VOLUME,    /**< Virtual — touch right shoulder zone */
    ODROID_INPUT_MAX
};

/* Gamepad state — array of button states (0=released, 1=pressed) */
typedef struct {
    int values[ODROID_INPUT_MAX];
} odroid_gamepad_state;

/* Battery state (stub — P4 has no battery) */
typedef struct {
    int millivolts;
    int percentage;
} odroid_battery_state;

/**
 * @brief Initialize the gamepad input subsystem.
 * Initializes the USB HID gamepad host.
 */
void odroid_input_gamepad_init(void);

/**
 * @brief Read the current gamepad state.
 * Polls the USB HID gamepad and maps to Odroid button layout.
 * @param state Output gamepad state
 */
void odroid_input_gamepad_read(odroid_gamepad_state *state);

/**
 * @brief Read raw gamepad state (same as gamepad_read, returns by value).
 * @return Current gamepad state
 */
odroid_gamepad_state odroid_input_read_raw(void);

/**
 * @brief Initialize battery level monitoring (stub — no battery on P4).
 */
void odroid_input_battery_level_init(void);

/**
 * @brief Read battery level (stub — always returns 100%).
 * @param state Output battery state
 */
void odroid_input_battery_level_read(odroid_battery_state *state);

/**
 * @brief Enable/disable battery monitor (stub — no battery on P4).
 * @param enabled true to enable, false to disable
 */
void odroid_input_battery_monitor_enabled_set(bool enabled);

/**
 * @brief Raw ADC value from the analog paddle potentiometer on GPIO 52.
 * Updated every gamepad poll.  Range 0-4095 (12-bit).
 * -1 means paddle not initialised / not detected.
 */
extern volatile int odroid_paddle_adc_raw;

/**
 * @brief When true, X/Y gamepad buttons are NOT mapped to MENU/VOLUME.
 *        Set by emulators that use X/Y natively (e.g. SNES).
 */
extern bool odroid_input_xy_menu_disable;

/**
 * @brief When true, touch-panel virtual MENU/VOLUME buttons are disabled.
 *        Set during on-screen keyboard to prevent touch-key conflicts.
 */
extern bool odroid_input_touch_buttons_disable;

/**
 * @brief Initialise the ADC for the analog paddle on GPIO 52 (ADC2_CH3).
 * Call once before reading odroid_paddle_adc_raw.
 */
void odroid_paddle_adc_init(void);

/**
 * @brief Check if the custom GPIO gamepad is detected.
 * @return true if GPIO gamepad hardware is connected
 */
bool odroid_input_gpio_pad_detected(void);

/**
 * @brief Check if a USB HID gamepad is currently connected.
 * @return true if USB gamepad is connected
 */
bool odroid_input_usb_gamepad_connected(void);

#ifdef __cplusplus
}
#endif
