/*
 * USB HID Gamepad Host — ESP32-P4
 *
 * Reads wired USB gamepads via the board's USB 2.0 Type-A host port.
 * Auto-detects PS4 DualShock 4, PS5 DualSense, and generic HID gamepads.
 *
 * Three background tasks:
 *   1. usb_lib_task   — USB host library event loop (core 0)
 *   2. HID host task  — created by hid_host_install() (core 0)
 *   3. gamepad_task   — processes queued device events (core 1)
 */

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "usb/usb_host.h"
#include "usb/hid_host.h"
#include "usb/hid_usage_keyboard.h"
#include "usb/hid_usage_mouse.h"
#include "gamepad.h"

static const char *TAG = "gamepad";

/* ========================= Report Format Detection ========================= */

typedef enum {
    GP_FORMAT_UNKNOWN = 0,
    GP_FORMAT_PS4,      /* [id=0x01][LX][LY][RX][RY][hat+btn1][btn2][btn3][L2][R2]... */
    GP_FORMAT_PS5,      /* [id=0x01][LX][LY][RX][RY][L2][R2][cnt][hat+btn1][btn2][btn3]... */
    GP_FORMAT_GENERIC,  /* [LX][LY][RX][RY][hat+btn][btn]... or [id][LX][LY]... */
} gp_report_format_t;

/* ========================= Internal State ========================= */

static gamepad_state_t s_state;
static SemaphoreHandle_t s_mutex = NULL;
static QueueHandle_t s_event_queue = NULL;
static TaskHandle_t s_usb_task = NULL;
static TaskHandle_t s_gp_task = NULL;
static volatile bool s_running = false;
static gp_report_format_t s_format = GP_FORMAT_UNKNOWN;
static int s_detect_count = 0;  /* number of reports used for format detection */
static volatile int s_format_log_pending = 0;  /* deferred format detection log */
static volatile int s_format_log_len = 0;      /* report len for deferred log */
static volatile int s_raw_dump_count = 0;      /* how many raw dumps have been emitted */
static uint8_t s_raw_dump_buf[64];             /* buffer for deferred raw hex dump */
static volatile int s_raw_dump_len = 0;

/* ========================= Event Queue ========================= */

typedef enum {
    GP_EVT_DEVICE_CONNECTED,
} gp_evt_type_t;

typedef struct {
    gp_evt_type_t type;
    hid_host_device_handle_t hid_handle;
} gp_evt_t;

/* ========================= Hat Switch → D-pad ========================= */

static uint8_t hat_to_dpad(uint8_t hat)
{
    static const uint8_t map[] = {
        GAMEPAD_DPAD_UP,                                /* 0 = N  */
        GAMEPAD_DPAD_UP   | GAMEPAD_DPAD_RIGHT,         /* 1 = NE */
        GAMEPAD_DPAD_RIGHT,                             /* 2 = E  */
        GAMEPAD_DPAD_DOWN | GAMEPAD_DPAD_RIGHT,         /* 3 = SE */
        GAMEPAD_DPAD_DOWN,                              /* 4 = S  */
        GAMEPAD_DPAD_DOWN | GAMEPAD_DPAD_LEFT,          /* 5 = SW */
        GAMEPAD_DPAD_LEFT,                              /* 6 = W  */
        GAMEPAD_DPAD_UP   | GAMEPAD_DPAD_LEFT,          /* 7 = NW */
    };
    return (hat <= 7) ? map[hat] : 0;
}

/* ========================= Format Detection ========================= */

/**
 * Detect report format from the first few idle reports.
 * PS4: byte[5] is hat+buttons byte (hat in lower nibble, 0-8 when idle = 0x08 centered)
 * PS5: byte[5] is L2 trigger (0 when idle), byte[8] is hat+buttons (0x08 when idle)
 */
static gp_report_format_t detect_format(const uint8_t *data, int len)
{
    /* Short report or no report ID → generic */
    if (len < 10 || data[0] != 0x01) {
        return GP_FORMAT_GENERIC;
    }

    /* Long report with ID 0x01: PS4 or PS5 */
    /* Check idle axes (bytes 1-4 should be near 0x80 = center) */
    bool axes_near_center = (data[1] >= 0x70 && data[1] <= 0x90) &&
                            (data[2] >= 0x70 && data[2] <= 0x90) &&
                            (data[3] >= 0x70 && data[3] <= 0x90) &&
                            (data[4] >= 0x70 && data[4] <= 0x90);

    if (axes_near_center) {
        /* Idle state pattern:
         * PS4: byte[5]=0x08 (hat centered), byte[8]=0x00 (L2 trigger)
         * PS5: byte[5]=0x00 (L2 trigger),   byte[8]=0x08 (hat centered)
         */
        if (data[5] == 0x08 && data[8] != 0x08) return GP_FORMAT_PS4;
        if (data[5] == 0x00 && data[8] == 0x08) return GP_FORMAT_PS5;
    }

    /* Non-idle heuristic: check which byte position has a valid hat (0-8) */
    uint8_t b5_hat = data[5] & 0x0F;
    uint8_t b8_hat = data[8] & 0x0F;

    if (b5_hat <= 8 && b8_hat > 8) return GP_FORMAT_PS4;
    if (b8_hat <= 8 && b5_hat > 8) return GP_FORMAT_PS5;

    /* Default: PS4 format (most common for wired USB gamepads) */
    return GP_FORMAT_PS4;
}

/* ========================= Button Parsing Helpers ========================= */

static uint32_t parse_ps_buttons(uint8_t hat_btn_byte, uint8_t btn2_byte, uint8_t btn3_byte)
{
    uint32_t btns = 0;

    /* hat_btn_byte upper nibble: face buttons */
    if (hat_btn_byte & 0x10) btns |= GAMEPAD_BTN_X;       /* Square */
    if (hat_btn_byte & 0x20) btns |= GAMEPAD_BTN_A;       /* Cross  */
    if (hat_btn_byte & 0x40) btns |= GAMEPAD_BTN_B;       /* Circle */
    if (hat_btn_byte & 0x80) btns |= GAMEPAD_BTN_Y;       /* Triangle */

    /* btn2_byte: shoulder + system */
    if (btn2_byte & 0x01) btns |= GAMEPAD_BTN_L1;
    if (btn2_byte & 0x02) btns |= GAMEPAD_BTN_R1;
    if (btn2_byte & 0x04) btns |= GAMEPAD_BTN_L2;
    if (btn2_byte & 0x08) btns |= GAMEPAD_BTN_R2;
    if (btn2_byte & 0x10) btns |= GAMEPAD_BTN_SELECT;     /* Share/Create */
    if (btn2_byte & 0x20) btns |= GAMEPAD_BTN_START;      /* Options */
    if (btn2_byte & 0x40) btns |= GAMEPAD_BTN_L3;
    if (btn2_byte & 0x80) btns |= GAMEPAD_BTN_R3;

    /* btn3_byte: PS + touchpad */
    if (btn3_byte & 0x01) btns |= GAMEPAD_BTN_HOME;
    if (btn3_byte & 0x02) btns |= GAMEPAD_BTN_MISC;       /* Touchpad click */

    return btns;
}

/* ========================= Report Parsing ========================= */

static void parse_gamepad_report(const uint8_t *data, int len)
{
    if (len < 4) return;

    /* Log first 16 bytes of raw report at DEBUG level */
    ESP_LOG_BUFFER_HEX_LEVEL(TAG, data, (len > 16) ? 16 : len, ESP_LOG_DEBUG);

    /* Always capture latest raw report for gamepad_get_raw_report() API */
    {
        int copy_len = (len > 64) ? 64 : len;
        memcpy(s_raw_dump_buf, data, copy_len);
        s_raw_dump_len = copy_len;
    }

    /* Auto-detect format from first few reports — NO LOGGING here (USB callback context) */
    if (s_format == GP_FORMAT_UNKNOWN && s_detect_count < 5) {
        gp_report_format_t detected = detect_format(data, len);
        if (detected != GP_FORMAT_UNKNOWN) {
            s_format = detected;
            s_format_log_len = len;
            s_format_log_pending = 1;  /* defer logging to gamepad_task */
        }
        s_detect_count++;
        if (s_detect_count >= 5 && s_format == GP_FORMAT_UNKNOWN) {
            s_format = (len >= 10 && data[0] == 0x01) ? GP_FORMAT_PS4 : GP_FORMAT_GENERIC;
            s_format_log_len = len;
            s_format_log_pending = 1;
        }
        if (s_format == GP_FORMAT_UNKNOWN) return;
    }

    gamepad_state_t gs;
    memset(&gs, 0, sizeof(gs));
    gs.connected = 1;

    switch (s_format) {
    case GP_FORMAT_PS4:
        /* PS4: [0x01][LX][LY][RX][RY][hat+btn1][btn2][btn3][L2][R2]... */
        if (len < 10) break;
        gs.axis_lx  = (int16_t)data[1] - 128;
        gs.axis_ly  = (int16_t)data[2] - 128;
        gs.axis_rx  = (int16_t)data[3] - 128;
        gs.axis_ry  = (int16_t)data[4] - 128;
        gs.dpad     = hat_to_dpad(data[5] & 0x0F);
        gs.buttons  = parse_ps_buttons(data[5], data[6], data[7]);
        gs.brake    = ((uint16_t)data[8]) << 2;   /* L2: 0-255 → 0-1020 */
        gs.throttle = ((uint16_t)data[9]) << 2;   /* R2: 0-255 → 0-1020 */
        break;

    case GP_FORMAT_PS5:
        /* PS5: [0x01][LX][LY][RX][RY][L2][R2][cnt][hat+btn1][btn2][btn3]... */
        if (len < 11) break;
        gs.axis_lx  = (int16_t)data[1] - 128;
        gs.axis_ly  = (int16_t)data[2] - 128;
        gs.axis_rx  = (int16_t)data[3] - 128;
        gs.axis_ry  = (int16_t)data[4] - 128;
        gs.brake    = ((uint16_t)data[5]) << 2;
        gs.throttle = ((uint16_t)data[6]) << 2;
        gs.dpad     = hat_to_dpad(data[8] & 0x0F);
        gs.buttons  = parse_ps_buttons(data[8], data[9], data[10]);
        break;

    case GP_FORMAT_GENERIC: {
        /*
         * Generic USB gamepad layout (8-byte, NO report ID):
         *
         *   [0]=LX  [1]=LY  [2]=RX  [3]=RY  [4]=Z-axis(0x80)  [5]=hat_lo|btns_hi  [6]=btns_lo  [7]=unused
         *
         *   Byte 5 low nibble:  hat switch (0x0F = centered, 0-7 = directions)
         *   Byte 5 high nibble: bit4=X, bit5=A, bit6=B, bit7=Y
         *   Byte 6:             bit0=L1, bit1=R1, bit4=Select, bit5=Start
         *   Byte 7:             unused on this pad
         *
         *   D-pad:  mapped to Left-stick axes (byte0=X, byte1=Y):
         *           Up=LY(0x00), Down=LY(0xFF), Left=LX(0x00), Right=LX(0xFF)
         *           Center = 0x7F
         */
        int off = 0;
        if (len > 8 && data[0] >= 0x01 && data[0] <= 0x0F) {
            off = 1;  /* Skip report ID */
        }
        if (len - off < 4) break;

        gs.axis_lx = (int16_t)data[off + 0] - 128;
        gs.axis_ly = (int16_t)data[off + 1] - 128;
        if (len - off >= 4) {
            gs.axis_rx = (int16_t)data[off + 2] - 128;
            gs.axis_ry = (int16_t)data[off + 3] - 128;
        }

        /* D-pad from LX/LY axes (this gamepad uses axes, not hat, for d-pad) */
        {
            uint8_t lx = data[off + 0];
            uint8_t ly = data[off + 1];
            /* Threshold: <0x20 = min, >0xE0 = max, else center */
            if (ly < 0x20) gs.dpad |= GAMEPAD_DPAD_UP;
            if (ly > 0xE0) gs.dpad |= GAMEPAD_DPAD_DOWN;
            if (lx < 0x20) gs.dpad |= GAMEPAD_DPAD_LEFT;
            if (lx > 0xE0) gs.dpad |= GAMEPAD_DPAD_RIGHT;
        }

        /* Also check hat in case another gamepad uses it: byte 5 low nibble */
        if (len - off >= 6) {
            uint8_t hat_val = data[off + 5] & 0x0F;
            if (hat_val <= 7) {
                gs.dpad |= hat_to_dpad(hat_val);
            }
        }

        /* Face buttons: byte 5 high nibble */
        if (len - off >= 6) {
            uint8_t bh5 = data[off + 5];
            if (bh5 & 0x10) gs.buttons |= GAMEPAD_BTN_X;
            if (bh5 & 0x20) gs.buttons |= GAMEPAD_BTN_A;
            if (bh5 & 0x40) gs.buttons |= GAMEPAD_BTN_B;
            if (bh5 & 0x80) gs.buttons |= GAMEPAD_BTN_Y;
        }

        /* Shoulder + meta buttons: byte 6 */
        if (len - off >= 7) {
            uint8_t bl = data[off + 6];
            if (bl & 0x01) gs.buttons |= GAMEPAD_BTN_L1;
            if (bl & 0x02) gs.buttons |= GAMEPAD_BTN_R1;
            if (bl & 0x10) gs.buttons |= GAMEPAD_BTN_SELECT;
            if (bl & 0x20) gs.buttons |= GAMEPAD_BTN_START;
        }
        break;
    }
    default:
        return;
    }

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    memcpy(&s_state, &gs, sizeof(gs));
    xSemaphoreGive(s_mutex);
}

/* ========================= HID Host Callbacks ========================= */

/**
 * Interface-level callback: receives input reports and disconnect events
 */
static void hid_interface_cb(hid_host_device_handle_t hid_dev,
                              const hid_host_interface_event_t event,
                              void *arg)
{
    switch (event) {
    case HID_HOST_INTERFACE_EVENT_INPUT_REPORT: {
        uint8_t data[64];
        size_t data_len = 0;
        esp_err_t err = hid_host_device_get_raw_input_report_data(hid_dev, data, sizeof(data), &data_len);
        if (err == ESP_OK && data_len > 0) {
            hid_host_dev_params_t params;
            hid_host_device_get_params(hid_dev, &params);
            /* Only parse non-keyboard, non-mouse devices */
            if (params.proto != HID_PROTOCOL_KEYBOARD && params.proto != HID_PROTOCOL_MOUSE) {
                parse_gamepad_report(data, (int)data_len);
            }
        }
        break;
    }
    case HID_HOST_INTERFACE_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "USB HID device disconnected");
        hid_host_device_close(hid_dev);
        /* Clear state */
        xSemaphoreTake(s_mutex, portMAX_DELAY);
        memset(&s_state, 0, sizeof(s_state));
        xSemaphoreGive(s_mutex);
        /* Reset format detection for next device */
        s_format = GP_FORMAT_UNKNOWN;
        s_detect_count = 0;
        break;
    case HID_HOST_INTERFACE_EVENT_TRANSFER_ERROR:
        /* Transfer error — most likely device disconnecting.
         * Do NOT close device here! The USB Host Library will fire DEV_GONE → DISCONNECTED
         * through the normal disconnect flow once it finishes cleaning up pending URBs.
         * Closing here would race the USB core and leave interfaces unreleased. */
        ESP_LOGW(TAG, "HID transfer error — waiting for USB core disconnect flow");
        break;
    default:
        break;
    }
}

/**
 * Driver-level callback: new HID device detected → queue for gamepad_task
 */
static void hid_device_cb(hid_host_device_handle_t hid_dev,
                           const hid_host_driver_event_t event,
                           void *arg)
{
    if (event == HID_HOST_DRIVER_EVENT_CONNECTED) {
        gp_evt_t evt = {
            .type = GP_EVT_DEVICE_CONNECTED,
            .hid_handle = hid_dev,
        };
        if (s_event_queue) {
            xQueueSend(s_event_queue, &evt, 0);
        }
    }
}

/* ========================= USB Host Library Task ========================= */

static void usb_lib_task(void *arg)
{
    const usb_host_config_t host_config = {
        .skip_phy_setup = false,
        .intr_flags = ESP_INTR_FLAG_LEVEL3,  /* Higher priority for USB interrupt */
    };

    ESP_ERROR_CHECK(usb_host_install(&host_config));
    ESP_LOGI(TAG, "USB host library installed");
    xTaskNotifyGive((TaskHandle_t)arg);

    while (s_running) {
        uint32_t event_flags;
        usb_host_lib_handle_events(portMAX_DELAY, &event_flags);
        if (event_flags & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS) {
            usb_host_device_free_all();
            if (!s_running) break;
        }
    }

    ESP_LOGI(TAG, "USB host library shutting down");
    vTaskDelay(pdMS_TO_TICKS(10));
    usb_host_uninstall();
    vTaskDelete(NULL);
}

/* ========================= Gamepad Processing Task ========================= */

static const char *proto_names[] = { "NONE", "KEYBOARD", "MOUSE" };

static void gamepad_task(void *arg)
{
    gp_evt_t evt;

    ESP_LOGI(TAG, "Waiting for USB HID gamepad...");

    while (s_running) {
        /* Deferred format detection log (avoid logging in USB callback context) */
        if (s_format_log_pending) {
            s_format_log_pending = 0;
            const char *names[] = {"unknown", "PS4", "PS5", "Generic"};
            ESP_LOGI(TAG, "Detected report format: %s (len=%d)", names[s_format], s_format_log_len);
        }

        /* Deferred raw hex dump (first 3 reports only, for initial format debugging) */
        if (s_raw_dump_count < 3 && s_raw_dump_len > 0) {
            s_raw_dump_count++;
            ESP_LOGI(TAG, "Raw report (%d bytes):", s_raw_dump_len);
            ESP_LOG_BUFFER_HEX_LEVEL(TAG, s_raw_dump_buf, s_raw_dump_len, ESP_LOG_INFO);
        }

        if (xQueueReceive(s_event_queue, &evt, pdMS_TO_TICKS(50))) {
            if (evt.type == GP_EVT_DEVICE_CONNECTED) {
                hid_host_dev_params_t params;
                ESP_ERROR_CHECK(hid_host_device_get_params(evt.hid_handle, &params));

                int proto_idx = (params.proto <= HID_PROTOCOL_MOUSE) ? params.proto : 0;
                ESP_LOGI(TAG, "HID device connected: sub_class=%d, proto=%s",
                         params.sub_class, proto_names[proto_idx]);

                if (params.proto != HID_PROTOCOL_KEYBOARD && params.proto != HID_PROTOCOL_MOUSE) {
                    ESP_LOGI(TAG, "==> Gamepad detected!");
                }

                /* Open device and configure interface callback */
                const hid_host_device_config_t dev_config = {
                    .callback = hid_interface_cb,
                    .callback_arg = NULL,
                };

                esp_err_t err = hid_host_device_open(evt.hid_handle, &dev_config);
                if (err != ESP_OK) {
                    ESP_LOGW(TAG, "hid_host_device_open failed: %s", esp_err_to_name(err));
                    continue;
                }

                /* Boot-interface devices: set protocol */
                if (HID_SUBCLASS_BOOT_INTERFACE == params.sub_class) {
                    hid_class_request_set_protocol(evt.hid_handle, HID_REPORT_PROTOCOL_BOOT);
                }

                /* NOTE: Removed SET_IDLE — PS4/PS5 gamepads may STALL this request,
                 * and the HID host library doesn't recover EP0 from STALL properly.
                 * SET_IDLE is only required for boot keyboards. */

                /* Let device settle after SET_CONFIGURATION. PS4 controllers may
                 * draw high current that causes VBUS droop → brief disconnect.
                 * Long delay gives power supply time to stabilize. */
                ESP_LOGI(TAG, "Waiting 1000ms for device to stabilize...");
                vTaskDelay(pdMS_TO_TICKS(1000));

                /* Try to start with retry logic */
                err = ESP_FAIL;
                for (int attempt = 0; attempt < 3; attempt++) {
                    err = hid_host_device_start(evt.hid_handle);
                    if (err == ESP_OK) {
                        ESP_LOGI(TAG, "hid_host_device_start OK (attempt %d)", attempt + 1);
                        break;
                    }
                    ESP_LOGW(TAG, "hid_host_device_start failed (attempt %d): %s",
                             attempt + 1, esp_err_to_name(err));
                    vTaskDelay(pdMS_TO_TICKS(100));
                }
                if (err != ESP_OK) {
                    ESP_LOGE(TAG, "hid_host_device_start giving up after 3 attempts");
                    hid_host_device_close(evt.hid_handle);
                    continue;
                }

                /* Mark connected for gamepad devices */
                if (params.proto != HID_PROTOCOL_KEYBOARD && params.proto != HID_PROTOCOL_MOUSE) {
                    xSemaphoreTake(s_mutex, portMAX_DELAY);
                    s_state.connected = 1;
                    xSemaphoreGive(s_mutex);
                }
            }
        }
    }

    vTaskDelete(NULL);
}

/* ========================= Public API ========================= */

esp_err_t gamepad_init(const gamepad_config_t *config)
{
    if (s_running) {
        return ESP_ERR_INVALID_STATE;
    }

    memset(&s_state, 0, sizeof(s_state));
    s_format = GP_FORMAT_UNKNOWN;
    s_detect_count = 0;
    s_format_log_pending = 0;
    s_raw_dump_count = 0;
    s_raw_dump_len = 0;

    s_mutex = xSemaphoreCreateMutex();
    if (!s_mutex) return ESP_ERR_NO_MEM;

    s_event_queue = xQueueCreate(10, sizeof(gp_evt_t));
    if (!s_event_queue) {
        vSemaphoreDelete(s_mutex);
        return ESP_ERR_NO_MEM;
    }

    s_running = true;

    /* Start USB host library task */
    BaseType_t ret = xTaskCreatePinnedToCore(usb_lib_task, "usb_host",
                                              config->usb_task_stack,
                                              xTaskGetCurrentTaskHandle(),
                                              config->usb_task_priority,
                                              &s_usb_task, 0);
    if (ret != pdTRUE) {
        ESP_LOGE(TAG, "Failed to create USB host task");
        s_running = false;
        vQueueDelete(s_event_queue);
        vSemaphoreDelete(s_mutex);
        return ESP_ERR_NO_MEM;
    }

    /* Wait for USB host to be ready */
    ulTaskNotifyTake(false, pdMS_TO_TICKS(3000));

    /* Install HID host driver (creates its own background task) */
    const hid_host_driver_config_t hid_config = {
        .create_background_task = true,
        .task_priority = config->hid_task_priority,
        .stack_size = config->hid_task_stack,
        .core_id = 0,
        .callback = hid_device_cb,
        .callback_arg = NULL,
    };

    esp_err_t err = hid_host_install(&hid_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to install HID host driver: %s", esp_err_to_name(err));
        s_running = false;
        /* USB host will shut down in its task when s_running becomes false */
        vQueueDelete(s_event_queue);
        vSemaphoreDelete(s_mutex);
        return err;
    }

    /* Start gamepad event processing task */
    ret = xTaskCreatePinnedToCore(gamepad_task, "gamepad", 4096, NULL,
                                   config->hid_task_priority, &s_gp_task, 1);
    if (ret != pdTRUE) {
        ESP_LOGE(TAG, "Failed to create gamepad task");
        hid_host_uninstall();
        s_running = false;
        vQueueDelete(s_event_queue);
        vSemaphoreDelete(s_mutex);
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "USB HID gamepad host initialized — plug in a USB gamepad!");
    return ESP_OK;
}

void gamepad_deinit(void)
{
    if (!s_running) return;

    s_running = false;

    /* Wait for tasks to finish */
    vTaskDelay(pdMS_TO_TICKS(500));

    hid_host_uninstall();

    if (s_event_queue) {
        vQueueDelete(s_event_queue);
        s_event_queue = NULL;
    }
    if (s_mutex) {
        vSemaphoreDelete(s_mutex);
        s_mutex = NULL;
    }

    s_usb_task = NULL;
    s_gp_task = NULL;
    memset(&s_state, 0, sizeof(s_state));
}

void gamepad_get_state(gamepad_state_t *state)
{
    if (!state || !s_mutex) {
        if (state) memset(state, 0, sizeof(*state));
        return;
    }
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    memcpy(state, &s_state, sizeof(gamepad_state_t));
    xSemaphoreGive(s_mutex);
}

bool gamepad_is_connected(void)
{
    if (!s_mutex) return false;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    bool conn = (s_state.connected != 0);
    xSemaphoreGive(s_mutex);
    return conn;
}

void gamepad_buttons_to_str(uint32_t buttons, char *buf, size_t buf_size)
{
    if (!buf || buf_size == 0) return;
    buf[0] = '\0';

    static const struct { uint32_t mask; const char *name; } btn_names[] = {
        { GAMEPAD_BTN_A,      "A"      },
        { GAMEPAD_BTN_B,      "B"      },
        { GAMEPAD_BTN_X,      "X"      },
        { GAMEPAD_BTN_Y,      "Y"      },
        { GAMEPAD_BTN_L1,     "L1"     },
        { GAMEPAD_BTN_R1,     "R1"     },
        { GAMEPAD_BTN_L2,     "L2"     },
        { GAMEPAD_BTN_R2,     "R2"     },
        { GAMEPAD_BTN_SELECT, "SEL"    },
        { GAMEPAD_BTN_START,  "START"  },
        { GAMEPAD_BTN_L3,     "L3"     },
        { GAMEPAD_BTN_R3,     "R3"     },
        { GAMEPAD_BTN_HOME,   "HOME"   },
        { GAMEPAD_BTN_MISC,   "MISC"   },
    };

    size_t pos = 0;
    for (int i = 0; i < sizeof(btn_names) / sizeof(btn_names[0]); i++) {
        if (buttons & btn_names[i].mask) {
            int written = snprintf(buf + pos, buf_size - pos,
                                   "%s%s", (pos > 0) ? " " : "", btn_names[i].name);
            if (written > 0) pos += written;
            if (pos >= buf_size - 1) break;
        }
    }

    if (pos == 0) {
        snprintf(buf, buf_size, "(none)");
    }
}

int gamepad_get_raw_report(uint8_t *buf, size_t buf_size)
{
    if (!buf || buf_size == 0 || s_raw_dump_len == 0) return 0;
    int copy = (s_raw_dump_len < (int)buf_size) ? s_raw_dump_len : (int)buf_size;
    memcpy(buf, s_raw_dump_buf, copy);
    return copy;
}
