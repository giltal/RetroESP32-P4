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
#include "gamepad.h"   /* gamepad_is_connected() */
#include "gt911_touch.h"

#include <string.h>
#include "esp_log.h"
#include "esp_adc/adc_oneshot.h"
#include "soc/adc_channel.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

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

/* ─── Paddle ADC (GPIO 51 = ADC2_CH2 on ESP32-P4) ───────────────── */
#define PADDLE_ADC_UNIT    ADC_UNIT_2
#define PADDLE_ADC_CHANNEL ADC_CHANNEL_2   /* GPIO 51 */

/* ─── Custom GPIO Gamepad (active when physical board detected) ──── */
/*  Analog inputs (shared ADC2):                                      */
/*    GPIO 49 (ADC2_CH0) — Joy left/right                            */
/*    GPIO 50 (ADC2_CH1) — Joy up/down                               */
/*    GPIO 52 (ADC2_CH3) — A/B buttons                               */
/*  Digital inputs:                                                   */
/*    GPIO 29 — L1 (phys pull-down; also used for detection)          */
/*    GPIO 30 — L2 (phys pull-down)                                   */
/*    GPIO 35 — X  (no phys pull-down, needs internal pull-down)      */
/*    GPIO 34 — Y  (phys pull-down)                                   */
/*    GPIO 28 — Start (phys pull-down)                                */
/*    GPIO 32 — Select (phys pull-down)                               */
#define GPIO_PAD_JOY_LR   49      /* ADC2_CH0 */
#define GPIO_PAD_JOY_UD   50      /* ADC2_CH1 */
#define GPIO_PAD_AB        52      /* ADC2_CH3 */
#define GPIO_PAD_L1        29
#define GPIO_PAD_L2        30
#define GPIO_PAD_X         33
#define GPIO_PAD_Y         34
#define GPIO_PAD_START     28
#define GPIO_PAD_SELECT    32

/* ADC thresholds for shared-pin analog buttons/joystick */
#define ADC_THRESH_HIGH    2800    /* single press: > 2800 (measured ~3300) */
#define ADC_THRESH_MID_LO  1200   /* second press: 1200..2200 (measured ~1650) */
#define ADC_THRESH_MID_HI  2200

static bool s_gpio_pad_detected = false;
static adc_oneshot_unit_handle_t s_gpio_pad_adc = NULL;

volatile int odroid_paddle_adc_raw = -1;
bool odroid_input_xy_menu_disable = false;
bool odroid_input_touch_buttons_disable = false;
static adc_oneshot_unit_handle_t s_paddle_adc_handle = NULL;

/* ─── GPIO gamepad detection & init ──────────────────────────────── */
static void gpio_pad_detect_and_init(void)
{
    /* Detection: pull-up GPIO 29 (L1), read. If 0 → custom pad connected
       (the physical pull-down on the gamepad board wins). */
    gpio_config_t detect_cfg = {
        .pin_bit_mask  = 1ULL << GPIO_PAD_L1,
        .mode          = GPIO_MODE_INPUT,
        .pull_up_en    = GPIO_PULLUP_ENABLE,
        .pull_down_en  = GPIO_PULLDOWN_DISABLE,
        .intr_type     = GPIO_INTR_DISABLE,
    };
    gpio_config(&detect_cfg);
    vTaskDelay(pdMS_TO_TICKS(5));   /* let the pull settle */

    if (gpio_get_level(GPIO_PAD_L1) != 0) {
        ESP_LOGI(TAG, "GPIO gamepad not detected (GPIO %d reads HIGH)", GPIO_PAD_L1);
        /* Reconfigure L1 pin back to default to avoid interfering */
        gpio_reset_pin(GPIO_PAD_L1);
        return;
    }

    ESP_LOGI(TAG, "GPIO gamepad DETECTED (GPIO %d reads LOW)", GPIO_PAD_L1);
    s_gpio_pad_detected = true;

    /* Configure digital button GPIOs as input */
    const int dig_pins[] = { GPIO_PAD_L1, GPIO_PAD_L2, GPIO_PAD_Y,
                             GPIO_PAD_START, GPIO_PAD_SELECT };
    for (int i = 0; i < sizeof(dig_pins)/sizeof(dig_pins[0]); i++) {
        gpio_config_t cfg = {
            .pin_bit_mask  = 1ULL << dig_pins[i],
            .mode          = GPIO_MODE_INPUT,
            .pull_up_en    = GPIO_PULLUP_DISABLE,
            .pull_down_en  = GPIO_PULLDOWN_DISABLE,  /* physical pull-down on board */
            .intr_type     = GPIO_INTR_DISABLE,
        };
        gpio_config(&cfg);
    }

    /* GPIO 33 (X) has physical pull-down */
    {
        gpio_config_t cfg = {
            .pin_bit_mask  = 1ULL << GPIO_PAD_X,
            .mode          = GPIO_MODE_INPUT,
            .pull_up_en    = GPIO_PULLUP_DISABLE,
            .pull_down_en  = GPIO_PULLDOWN_DISABLE,
            .intr_type     = GPIO_INTR_DISABLE,
        };
        gpio_config(&cfg);
    }

    /* Set up ADC2 for the three analog inputs (CH0=joy LR, CH1=joy UD, CH3=AB) */
    if (!s_gpio_pad_adc) {
        adc_oneshot_unit_init_cfg_t unit_cfg = {
            .unit_id = ADC_UNIT_2,
        };
        esp_err_t adc_err = adc_oneshot_new_unit(&unit_cfg, &s_gpio_pad_adc);
        if (adc_err != ESP_OK) {
            ESP_LOGW(TAG, "GPIO pad: ADC2 unit init failed (%s)", esp_err_to_name(adc_err));
            s_gpio_pad_detected = false;
            return;
        }

        adc_oneshot_chan_cfg_t chan_cfg = {
            .atten   = ADC_ATTEN_DB_12,
            .bitwidth = ADC_BITWIDTH_12,
        };
        adc_oneshot_config_channel(s_gpio_pad_adc, ADC_CHANNEL_0, &chan_cfg);  /* GPIO 49 LR */
        adc_oneshot_config_channel(s_gpio_pad_adc, ADC_CHANNEL_1, &chan_cfg);  /* GPIO 50 UD */
        adc_oneshot_config_channel(s_gpio_pad_adc, ADC_CHANNEL_3, &chan_cfg);  /* GPIO 52 AB */
    }

    ESP_LOGI(TAG, "GPIO gamepad initialized: analog(49,50,52) digital(28,29,30,32,34,35)");
}

/* Read the custom GPIO gamepad and OR into the state */
static void gpio_pad_read(odroid_gamepad_state *state)
{
    if (!s_gpio_pad_detected || !s_gpio_pad_adc) return;

    int joy_lr = 0, joy_ud = 0, ab_val = 0;
    adc_oneshot_read(s_gpio_pad_adc, ADC_CHANNEL_0, &joy_lr);
    adc_oneshot_read(s_gpio_pad_adc, ADC_CHANNEL_1, &joy_ud);
    adc_oneshot_read(s_gpio_pad_adc, ADC_CHANNEL_3, &ab_val);

    /* Joy left/right (GPIO 49) */
    if (joy_lr > ADC_THRESH_HIGH)
        state->values[ODROID_INPUT_LEFT] = 1;
    else if (joy_lr > ADC_THRESH_MID_LO && joy_lr < ADC_THRESH_MID_HI)
        state->values[ODROID_INPUT_RIGHT] = 1;

    /* Joy up/down (GPIO 50) */
    if (joy_ud > ADC_THRESH_HIGH)
        state->values[ODROID_INPUT_UP] = 1;
    else if (joy_ud > ADC_THRESH_MID_LO && joy_ud < ADC_THRESH_MID_HI)
        state->values[ODROID_INPUT_DOWN] = 1;

    /* A/B buttons (GPIO 52) */
    if (ab_val > ADC_THRESH_HIGH)
        state->values[ODROID_INPUT_A] = 1;
    else if (ab_val > ADC_THRESH_MID_LO && ab_val < ADC_THRESH_MID_HI)
        state->values[ODROID_INPUT_B] = 1;

    /* Digital buttons — all active HIGH (pressed = 1) */
    if (gpio_get_level(GPIO_PAD_L1))     state->values[ODROID_INPUT_L] = 1;
    if (gpio_get_level(GPIO_PAD_L2))     state->values[ODROID_INPUT_R] = 1;
    if (gpio_get_level(GPIO_PAD_X))      state->values[ODROID_INPUT_X] = 1;
    if (gpio_get_level(GPIO_PAD_Y))      state->values[ODROID_INPUT_Y] = 1;
    if (gpio_get_level(GPIO_PAD_START))  state->values[ODROID_INPUT_START] = 1;
    if (gpio_get_level(GPIO_PAD_SELECT)) state->values[ODROID_INPUT_SELECT] = 1;

    /* Read paddle ADC (GPIO 51 = ADC2_CH2) if initialized */
    if (s_paddle_adc_handle) {
        int raw = 0;
        if (adc_oneshot_read(s_paddle_adc_handle, PADDLE_ADC_CHANNEL, &raw) == ESP_OK) {
            odroid_paddle_adc_raw = raw;
        }
    }
}

void odroid_input_gamepad_init(void)
{
    if (s_initialized) return;

    /* Detect and init custom GPIO gamepad (if connected) */
    gpio_pad_detect_and_init();

    /* USB gamepad is initialized in odroid_system_init() */
    s_initialized = true;
    ESP_LOGI(TAG, "Input subsystem ready (USB HID gamepad%s)",
             s_gpio_pad_detected ? " + GPIO gamepad" : "");
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

        /* Read paddle potentiometer if ADC has been initialised and
           GPIO gamepad is NOT active (GPIO pad reads paddle itself) */
        if (!s_gpio_pad_detected && s_paddle_adc_handle) {
            int raw = 0;
            if (adc_oneshot_read(s_paddle_adc_handle, PADDLE_ADC_CHANNEL, &raw) == ESP_OK) {
                odroid_paddle_adc_raw = raw;
            }
        }
    }

    /* Custom GPIO gamepad — OR its buttons into the state */
    gpio_pad_read(state);

    /* Touch-panel virtual shoulder buttons — sampled at 2 Hz to avoid CPU overhead.
     * Disabled when touch keyboard is active (odroid_input_touch_buttons_disable). */
    if (!odroid_input_touch_buttons_disable) {
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
    } else {
        s_touch_menu = 0;
        s_touch_volume = 0;
    }

    /* X → Menu, Y → Volume for emulators that lack native X/Y (skip for SNES/Genesis). */
    if (!odroid_input_xy_menu_disable) {
        state->values[ODROID_INPUT_MENU]   |= state->values[ODROID_INPUT_X];
        state->values[ODROID_INPUT_VOLUME] |= state->values[ODROID_INPUT_Y];
    }
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

    /* If the GPIO gamepad already created ADC2, reuse its handle */
    if (s_gpio_pad_adc) {
        s_paddle_adc_handle = s_gpio_pad_adc;
    } else {
        adc_oneshot_unit_init_cfg_t unit_cfg = {
            .unit_id = PADDLE_ADC_UNIT,
        };
        ESP_ERROR_CHECK(adc_oneshot_new_unit(&unit_cfg, &s_paddle_adc_handle));
    }

    adc_oneshot_chan_cfg_t chan_cfg = {
        .atten  = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_12,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(s_paddle_adc_handle,
                                               PADDLE_ADC_CHANNEL, &chan_cfg));
    ESP_LOGI(TAG, "Paddle ADC initialised: ADC2_CH2 (GPIO 51), 12-bit, 12dB atten");
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

bool odroid_input_gpio_pad_detected(void)
{
    return s_gpio_pad_detected;
}

bool odroid_input_usb_gamepad_connected(void)
{
    return gamepad_is_connected();
}
