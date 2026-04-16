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
#include <stdio.h>
#include <sys/stat.h>
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

/* ─── Battery ADC (GPIO 53 = ADC2_CH4 on ESP32-P4) ──────────────── */
/*  Voltage divider: 68K (battery side) + 100K (GND side)             */
/*  Vgpio = Vbat × 100 / 168  →  Vbat = Vgpio × 168 / 100           */
#define BATTERY_ADC_CHANNEL ADC_CHANNEL_4  /* GPIO 53 */
#define BATTERY_DIVIDER_NUM 168            /* R_high + R_low */
#define BATTERY_DIVIDER_DEN 100            /* R_low */

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
static adc_oneshot_unit_handle_t s_battery_adc_handle = NULL;

/* ─── USB Gamepad Button Mapping ─────────────────────────────────── */
/* Index: 0=A, 1=B, 2=X, 3=Y, 4=L, 5=R, 6=SELECT, 7=START */
static odroid_usb_map_t s_usb_map = {
    .btn = {
        GAMEPAD_BTN_A,                        /* A */
        GAMEPAD_BTN_B,                        /* B */
        GAMEPAD_BTN_X,                        /* X */
        GAMEPAD_BTN_Y,                        /* Y */
        GAMEPAD_BTN_L1 | GAMEPAD_BTN_L2,      /* L */
        GAMEPAD_BTN_R1 | GAMEPAD_BTN_R2,      /* R */
        GAMEPAD_BTN_SELECT,                   /* SELECT */
        GAMEPAD_BTN_START,                    /* START */
    }
};
static bool s_usb_map_loaded = false;
static uint16_t s_usb_map_vid = 0;  /* VID of the currently loaded map */
static uint16_t s_usb_map_pid = 0;  /* PID of the currently loaded map */

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
        /* Auto-load mapping for this controller if not yet loaded */
        uint16_t vid = 0, pid = 0;
        gamepad_get_vid_pid(&vid, &pid);
        if (!s_usb_map_loaded || vid != s_usb_map_vid || pid != s_usb_map_pid) {
            odroid_input_usb_map_load();
        }

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
        /* D-pad from mapped buttons (for controllers that report d-pad as buttons) */
        if (s_usb_map.btn[8]  && (gp.buttons & s_usb_map.btn[8]))  state->values[ODROID_INPUT_UP]    = 1;
        if (s_usb_map.btn[9]  && (gp.buttons & s_usb_map.btn[9]))  state->values[ODROID_INPUT_DOWN]  = 1;
        if (s_usb_map.btn[10] && (gp.buttons & s_usb_map.btn[10])) state->values[ODROID_INPUT_LEFT]  = 1;
        if (s_usb_map.btn[11] && (gp.buttons & s_usb_map.btn[11])) state->values[ODROID_INPUT_RIGHT] = 1;
        /* Buttons — from configurable mapping table */
        state->values[ODROID_INPUT_A]      = (gp.buttons & s_usb_map.btn[0]) ? 1 : 0;
        state->values[ODROID_INPUT_B]      = (gp.buttons & s_usb_map.btn[1]) ? 1 : 0;
        state->values[ODROID_INPUT_X]      = (gp.buttons & s_usb_map.btn[2]) ? 1 : 0;
        state->values[ODROID_INPUT_Y]      = (gp.buttons & s_usb_map.btn[3]) ? 1 : 0;
        state->values[ODROID_INPUT_L]      = (gp.buttons & s_usb_map.btn[4]) ? 1 : 0;
        state->values[ODROID_INPUT_R]      = (gp.buttons & s_usb_map.btn[5]) ? 1 : 0;

        /* System buttons */
        state->values[ODROID_INPUT_SELECT] = (gp.buttons & s_usb_map.btn[6]) ? 1 : 0;
        state->values[ODROID_INPUT_START]  = (gp.buttons & s_usb_map.btn[7]) ? 1 : 0;

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

    /* Reuse existing ADC2 handle if GPIO gamepad or battery already created it */
    if (s_gpio_pad_adc) {
        s_paddle_adc_handle = s_gpio_pad_adc;
    } else if (s_battery_adc_handle) {
        s_paddle_adc_handle = s_battery_adc_handle;
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
    if (s_battery_adc_handle) return;  /* already initialised */

    /* Reuse existing ADC2 handle if GPIO pad or paddle already created it */
    if (s_gpio_pad_adc) {
        s_battery_adc_handle = s_gpio_pad_adc;
    } else if (s_paddle_adc_handle) {
        s_battery_adc_handle = s_paddle_adc_handle;
    } else {
        adc_oneshot_unit_init_cfg_t unit_cfg = {
            .unit_id = ADC_UNIT_2,
        };
        esp_err_t err = adc_oneshot_new_unit(&unit_cfg, &s_battery_adc_handle);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Battery ADC: unit init failed (%s)", esp_err_to_name(err));
            return;
        }
    }

    adc_oneshot_chan_cfg_t chan_cfg = {
        .atten   = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_12,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(s_battery_adc_handle,
                                               BATTERY_ADC_CHANNEL, &chan_cfg));

    ESP_LOGI(TAG, "Battery ADC initialised: ADC2_CH4 (GPIO 53), divider 68K/100K");
}

void odroid_input_battery_level_read(odroid_battery_state *state)
{
    if (!state) return;

    if (!s_battery_adc_handle) {
        /* Not initialised — report full */
        state->millivolts = 4200;
        state->percentage = 100;
        return;
    }

    /* Average 4 samples for stability */
    int sum = 0;
    int ok_count = 0;
    for (int i = 0; i < 4; i++) {
        int raw = 0;
        esp_err_t err = adc_oneshot_read(s_battery_adc_handle, BATTERY_ADC_CHANNEL, &raw);
        if (err == ESP_OK) {
            sum += raw;
            ok_count++;
        }
    }
    int raw_avg = ok_count > 0 ? sum / ok_count : 0;

    /* Convert: raw → mV at GPIO → mV at battery.
     * ESP32-P4 ADC at 12dB atten reads ~15% low vs ideal 3300mV full-scale.
     * Empirical correction: measured 3333mV vs multimeter 3900mV → ×1.17.
     * Apply by using effective Vref of 3861mV (3300 × 1.17). */
    int gpio_mv = raw_avg * 3861 / 4095;
    int bat_mv  = gpio_mv * BATTERY_DIVIDER_NUM / BATTERY_DIVIDER_DEN;

    /* Detect charging: voltage above full-battery-under-load threshold */
    bool charging = (bat_mv > 3900);

    /* Clamp to practical LiPo range under load.
     * Full (just off charger, device running): ~3.9V
     * Empty (device about to shut off):        ~3.3V */
    if (bat_mv > 3900) bat_mv = 3900;
    if (bat_mv < 3300) bat_mv = 3300;

    /* Linear percentage: 3.3V = 0%, 3.9V = 100% */
    int pct = (bat_mv - 3300) * 100 / (3900 - 3300);
    if (pct > 100) pct = 100;
    if (pct < 0)   pct = 0;

    state->millivolts = bat_mv;
    state->percentage = pct;
    state->charging = charging;
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

/* ─── USB Gamepad Button Mapping — file I/O ──────────────────────── */

/* Map file format: 4-byte magic "GMAP", then ODROID_USB_MAP_COUNT × uint32_t LE */
#define GMAP_MAGIC 0x50414D47  /* "GMAP" little-endian */

static void usb_map_set_defaults(void)
{
    s_usb_map.btn[0] = GAMEPAD_BTN_A;
    s_usb_map.btn[1] = GAMEPAD_BTN_B;
    s_usb_map.btn[2] = GAMEPAD_BTN_X;
    s_usb_map.btn[3] = GAMEPAD_BTN_Y;
    s_usb_map.btn[4] = GAMEPAD_BTN_L1 | GAMEPAD_BTN_L2;
    s_usb_map.btn[5] = GAMEPAD_BTN_R1 | GAMEPAD_BTN_R2;
    s_usb_map.btn[6] = GAMEPAD_BTN_SELECT;
    s_usb_map.btn[7] = GAMEPAD_BTN_START;
    /* D-pad: 0 = use built-in hat switch / analog stick detection */
    s_usb_map.btn[8]  = 0;  /* UP */
    s_usb_map.btn[9]  = 0;  /* DOWN */
    s_usb_map.btn[10] = 0;  /* LEFT */
    s_usb_map.btn[11] = 0;  /* RIGHT */
}

static bool usb_map_get_path(char *buf, size_t buflen)
{
    uint16_t vid = 0, pid = 0;
    gamepad_get_vid_pid(&vid, &pid);
    if (vid == 0 && pid == 0) return false;
    snprintf(buf, buflen, "/sd/odroid/gamepad/%04X_%04X.map", vid, pid);
    return true;
}

bool odroid_input_usb_map_exists(void)
{
    char path[80];
    if (!usb_map_get_path(path, sizeof(path))) return false;
    struct stat st;
    return (stat(path, &st) == 0);
}

void odroid_input_usb_map_load(void)
{
    uint16_t vid = 0, pid = 0;
    gamepad_get_vid_pid(&vid, &pid);

    /* Default mapping first */
    usb_map_set_defaults();
    s_usb_map_loaded = true;
    s_usb_map_vid = vid;
    s_usb_map_pid = pid;

    char path[80];
    if (!usb_map_get_path(path, sizeof(path))) return;

    /* Try to read from SD — SD may or may not be mounted */
    FILE *f = fopen(path, "rb");
    if (!f) {
        ESP_LOGI(TAG, "No custom map for %04X:%04X, using defaults", vid, pid);
        return;
    }

    uint32_t magic = 0;
    if (fread(&magic, 4, 1, f) != 1 || magic != GMAP_MAGIC) {
        ESP_LOGW(TAG, "Invalid map file magic for %04X:%04X", vid, pid);
        fclose(f);
        return;
    }

    uint32_t data[ODROID_USB_MAP_COUNT];
    memset(data, 0, sizeof(data));
    size_t nread = fread(data, sizeof(uint32_t), ODROID_USB_MAP_COUNT, f);
    fclose(f);

    if (nread < 8) {
        ESP_LOGW(TAG, "Short map file for %04X:%04X (got %d entries)", vid, pid, (int)nread);
        usb_map_set_defaults();
        return;
    }
    /* Old 8-entry files: indices 8-11 stay 0 (use hat/axis fallback) */

    for (int i = 0; i < ODROID_USB_MAP_COUNT; i++)
        s_usb_map.btn[i] = data[i];

    ESP_LOGI(TAG, "Loaded custom map for %04X:%04X", vid, pid);
}

bool odroid_input_usb_map_save(const odroid_usb_map_t *map)
{
    if (!map) return false;

    char path[80];
    if (!usb_map_get_path(path, sizeof(path))) return false;

    /* Ensure directories exist — SD should already be mounted by caller */
    mkdir("/sd/odroid", 0775);
    mkdir("/sd/odroid/gamepad", 0775);

    FILE *f = fopen(path, "wb");
    if (!f) {
        ESP_LOGE(TAG, "Failed to create map file: %s", path);
        return false;
    }

    uint32_t magic = GMAP_MAGIC;
    fwrite(&magic, 4, 1, f);
    fwrite(map->btn, sizeof(uint32_t), ODROID_USB_MAP_COUNT, f);
    fclose(f);

    /* Apply immediately */
    memcpy(&s_usb_map, map, sizeof(odroid_usb_map_t));
    ESP_LOGI(TAG, "Saved map to %s", path);
    return true;
}

void odroid_input_usb_map_get(odroid_usb_map_t *map)
{
    if (map) memcpy(map, &s_usb_map, sizeof(odroid_usb_map_t));
}

void odroid_input_usb_map_set(const odroid_usb_map_t *map)
{
    if (map) memcpy(&s_usb_map, map, sizeof(odroid_usb_map_t));
}
