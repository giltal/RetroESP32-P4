/*
 * Odroid Input Compatibility Layer — ESP32-P4 Implementation
 *
 * Maps USB HID gamepad buttons to the native SNES layout plus
 * two touch-panel virtual buttons for menu and volume.
 *
 * Physical button mapping:
 *   D-pad / left-stick  → ODROID_INPUT_UP/DOWN/LEFT/RIGHT
 *   A (right face)      → ODROID_INPUT_A
 *   B (bottom face)     → ODROID_INPUT_B
 *   X (top face)        → ODROID_INPUT_X
 *   Y (left face)       → ODROID_INPUT_Y
 *   L1 / L2             → ODROID_INPUT_L
 *   R1 / R2             → ODROID_INPUT_R
 *   SELECT              → ODROID_INPUT_SELECT
 *   START               → ODROID_INPUT_START
 *
 * Touch virtual buttons (landscape orientation, portrait coords):
 *   Touch y < 170       → ODROID_INPUT_MENU   (left shoulder)
 *   Touch y > 630       → ODROID_INPUT_VOLUME (right shoulder)
 */

#include "odroid_input.h"
#include "gamepad.h"
#include "gt911_touch.h"

#include <string.h>
#include "esp_log.h"
#include "esp_adc/adc_oneshot.h"
#include "soc/adc_channel.h"
#include "esp_timer.h"

/* Touch zone thresholds (GT911 portrait coords, 480×800) */
#define TOUCH_MENU_Y_MAX   170   /* y < 170 → left shoulder (landscape left)  */
#define TOUCH_VOL_Y_MIN    630   /* y > 630 → right shoulder (landscape right) */

/* Touch panel is sampled at most 2 Hz (every 500 ms) to avoid ~10% CPU overhead */
#define TOUCH_POLL_INTERVAL_US  500000LL

static const char *TAG = "odroid_input";
static bool s_initialized = false;

/* Cached touch state — updated at 2 Hz */
static volatile int s_touch_menu   = 0;
static volatile int s_touch_volume = 0;
static int64_t      s_touch_last_us = 0;

/* ─── Paddle ADC (GPIO 52 = ADC2_CH3 on ESP32-P4) ───────────────── */
#define PADDLE_ADC_UNIT    ADC_UNIT_2
#define PADDLE_ADC_CHANNEL ADC_CHANNEL_3   /* GPIO 52 */

volatile int odroid_paddle_adc_raw = -1;
static adc_oneshot_unit_handle_t s_paddle_adc_handle = NULL;

void odroid_input_gamepad_init(void)
{
    if (s_initialized) return;

    /* USB gamepad is initialized in odroid_system_init() */
    s_initialized = true;
    ESP_LOGI(TAG, "Input subsystem ready (USB HID gamepad)");
}

void odroid_input_gamepad_read(odroid_gamepad_state *state)
{
    memset(state, 0, sizeof(odroid_gamepad_state));

    gamepad_state_t gp;
    gamepad_get_state(&gp);

    if (gp.connected) {
        /* D-pad — from dpad bitmask */
        state->values[ODROID_INPUT_UP]    = (gp.dpad & GAMEPAD_DPAD_UP)    ? 1 : 0;
        state->values[ODROID_INPUT_DOWN]  = (gp.dpad & GAMEPAD_DPAD_DOWN)  ? 1 : 0;
        state->values[ODROID_INPUT_LEFT]  = (gp.dpad & GAMEPAD_DPAD_LEFT)  ? 1 : 0;
        state->values[ODROID_INPUT_RIGHT] = (gp.dpad & GAMEPAD_DPAD_RIGHT) ? 1 : 0;

        /* Also map left analog stick as d-pad (threshold ±64 out of ±128) */
        if (gp.axis_ly < -64) state->values[ODROID_INPUT_UP]    = 1;
        if (gp.axis_ly >  64) state->values[ODROID_INPUT_DOWN]  = 1;
        if (gp.axis_lx < -64) state->values[ODROID_INPUT_LEFT]  = 1;
        if (gp.axis_lx >  64) state->values[ODROID_INPUT_RIGHT] = 1;

        /* Face buttons — native SNES layout (A=right, B=bottom, X=top, Y=left) */
        state->values[ODROID_INPUT_A]      = (gp.buttons & GAMEPAD_BTN_A)      ? 1 : 0;
        state->values[ODROID_INPUT_B]      = (gp.buttons & GAMEPAD_BTN_B)      ? 1 : 0;
        state->values[ODROID_INPUT_X]      = (gp.buttons & GAMEPAD_BTN_X)      ? 1 : 0;
        state->values[ODROID_INPUT_Y]      = (gp.buttons & GAMEPAD_BTN_Y)      ? 1 : 0;

        /* Shoulder buttons — L1 and L2 both trigger L; R1 and R2 both trigger R */
        state->values[ODROID_INPUT_L]      = ((gp.buttons & GAMEPAD_BTN_L1) ||
                                              (gp.buttons & GAMEPAD_BTN_L2)) ? 1 : 0;
        state->values[ODROID_INPUT_R]      = ((gp.buttons & GAMEPAD_BTN_R1) ||
                                              (gp.buttons & GAMEPAD_BTN_R2)) ? 1 : 0;

        /* System buttons */
        state->values[ODROID_INPUT_SELECT] = (gp.buttons & GAMEPAD_BTN_SELECT) ? 1 : 0;
        state->values[ODROID_INPUT_START]  = (gp.buttons & GAMEPAD_BTN_START)  ? 1 : 0;

        /* Read paddle potentiometer if ADC has been initialised */
        if (s_paddle_adc_handle) {
            int raw = 0;
            if (adc_oneshot_read(s_paddle_adc_handle, PADDLE_ADC_CHANNEL, &raw) == ESP_OK) {
                odroid_paddle_adc_raw = raw;
            }
        }
    }

    /* Touch-panel virtual shoulder buttons — sampled at 2 Hz to avoid CPU overhead */
    {
        int64_t now = esp_timer_get_time();
        if (now - s_touch_last_us >= TOUCH_POLL_INTERVAL_US) {
            s_touch_last_us = now;
            uint16_t tx = 0, ty = 0;
            int menu = 0, vol = 0;
            if (gt911_touch_get_xy(&tx, &ty)) {
                if (ty < TOUCH_MENU_Y_MAX)
                    menu = 1;
                else if (ty > TOUCH_VOL_Y_MIN)
                    vol = 1;
            }
            s_touch_menu   = menu;
            s_touch_volume = vol;
        }
        state->values[ODROID_INPUT_MENU]   |= s_touch_menu;
        state->values[ODROID_INPUT_VOLUME] |= s_touch_volume;
    }

    /* X → Menu, Y → Volume for all emulators (SNES also reads X/Y directly) */
    state->values[ODROID_INPUT_MENU]   |= state->values[ODROID_INPUT_X];
    state->values[ODROID_INPUT_VOLUME] |= state->values[ODROID_INPUT_Y];
}

odroid_gamepad_state odroid_input_read_raw(void)
{
    odroid_gamepad_state state;
    odroid_input_gamepad_read(&state);
    return state;
}

void odroid_paddle_adc_init(void)
{
    if (s_paddle_adc_handle) return;  /* already initialised */

    adc_oneshot_unit_init_cfg_t unit_cfg = {
        .unit_id = PADDLE_ADC_UNIT,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&unit_cfg, &s_paddle_adc_handle));

    adc_oneshot_chan_cfg_t chan_cfg = {
        .atten  = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_12,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(s_paddle_adc_handle,
                                               PADDLE_ADC_CHANNEL, &chan_cfg));
    ESP_LOGI(TAG, "Paddle ADC initialised: ADC2_CH3 (GPIO 52), 12-bit, 12dB atten");
}

void odroid_input_battery_level_init(void)
{
    /* No battery on ESP32-P4 — stub */
    ESP_LOGI(TAG, "Battery monitor: not available (no battery on P4)");
}

void odroid_input_battery_level_read(odroid_battery_state *state)
{
    if (!state) return;
    /* Always report full "battery" */
    state->millivolts = 4200;
    state->percentage = 100;
}

void odroid_input_battery_monitor_enabled_set(bool enabled)
{
    /* Stub — no battery on ESP32-P4 */
    (void)enabled;
}
