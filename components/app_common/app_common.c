/*
 * app_common.c — Shared init/exit for OTA-based emulator apps
 *
 * Crash-guard: Uses an NVS boot counter to detect crash loops.
 * If an emulator app crashes 3 times in a row before reaching
 * app_init() completion, the next boot automatically rolls back
 * to the launcher (factory partition).
 */
#include "app_common.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"

#include "odroid_system.h"
#include "odroid_display.h"
#include "odroid_settings.h"
#include "odroid_sdcard.h"
#include "odroid_audio.h"
#include "odroid_input.h"
#include "gt911_touch.h"

static const char *TAG = "app_common";

/* ─── Crash-guard: NVS boot counter ──────────────────────────── */
#define CRASH_GUARD_NVS_NS   "app_guard"
#define CRASH_GUARD_NVS_KEY  "boot_cnt"
#define CRASH_GUARD_MAX      3   /* rollback after this many consecutive crashes */

/**
 * Check boot counter in NVS.  Must be called RIGHT AFTER nvs_flash_init()
 * and BEFORE any hardware init (which is where crashes are likely).
 *
 * If counter >= CRASH_GUARD_MAX → rollback to launcher.
 * Otherwise → increment counter and save (will be cleared after successful init).
 */
static void crash_guard_check(void)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(CRASH_GUARD_NVS_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "crash_guard: can't open NVS ns (%s), skipping", esp_err_to_name(err));
        return;
    }

    uint8_t cnt = 0;
    nvs_get_u8(h, CRASH_GUARD_NVS_KEY, &cnt);  /* OK if not found — defaults to 0 */

    ESP_LOGI(TAG, "crash_guard: boot_count = %u (max %u)", cnt, CRASH_GUARD_MAX);

    if (cnt >= CRASH_GUARD_MAX) {
        ESP_LOGE(TAG, "crash_guard: %u consecutive crashes detected — rolling back to launcher!", cnt);
        /* Reset counter so launcher doesn't also trigger */
        nvs_set_u8(h, CRASH_GUARD_NVS_KEY, 0);
        nvs_commit(h);
        nvs_close(h);
        app_return_to_launcher();
        /* Does not return */
    }

    /* Increment counter — will be cleared after successful init */
    nvs_set_u8(h, CRASH_GUARD_NVS_KEY, cnt + 1);
    nvs_commit(h);
    nvs_close(h);
}

/**
 * Clear the crash-guard boot counter.
 */
static void crash_guard_clear(void)
{
    nvs_handle_t h;
    if (nvs_open(CRASH_GUARD_NVS_NS, NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_u8(h, CRASH_GUARD_NVS_KEY, 0);
        nvs_commit(h);
        nvs_close(h);
        ESP_LOGI(TAG, "crash_guard: boot counter cleared");
    }
}

/**
 * Timer callback — fires ~10 s after app_init() to clear the crash counter.
 * If the emulator crashes before this fires, the counter stays incremented.
 */
static void crash_guard_timer_cb(TimerHandle_t xTimer)
{
    crash_guard_clear();
    xTimerDelete(xTimer, 0);
}

/* ─── Full hardware init ──────────────────────────────────────── */
void app_init(void)
{
    ESP_LOGI(TAG, "=== Emulator App Init ===");

    /* 1. NVS — must be first! */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    /* 2. Crash-guard: detect crash loops BEFORE hardware init */
    crash_guard_check();

    /* 3. Hardware: I2C → PPA → USB → Audio → LCD → Touch */
    odroid_system_init();

    /* 3b. Allocate 320×240 virtual framebuffer + PPA output buffer */
    ili9341_init();

    /* 4. Audio at default sample rate (emulator will re-init at its own rate) */
    odroid_audio_init(16000);

    /* 5. Gamepad polling */
    odroid_input_gamepad_init();

    /* 6. Mount SD card */
    odroid_sdcard_open("/sd");

    /* 7. Arm a 10-second one-shot timer to clear the crash counter.
     *    If the emulator crashes before the timer fires, the counter
     *    stays incremented and the next boot will detect the loop. */
    TimerHandle_t guard_tmr = xTimerCreate(
        "guard_clr", pdMS_TO_TICKS(10000), pdFALSE, NULL, crash_guard_timer_cb);
    if (guard_tmr) xTimerStart(guard_tmr, 0);

    ESP_LOGI(TAG, "=== Emulator App Init Complete ===");

    /* 8. Safe-boot check: hold A during boot → return to launcher */
    app_check_safe_boot();
}

/* ─── Safe-boot: if touch screen is touched, return to launcher ── */
void app_check_safe_boot(void)
{
    ESP_LOGI(TAG, "Safe-boot check: touch screen to return to launcher...");

    /* Poll touch screen a few times over ~500ms.
     * USB gamepad isn't ready this early, but the GT911 touch controller
     * is already initialized by odroid_system_init(). */
    for (int i = 0; i < 10; i++) {
        uint16_t x, y;
        if (gt911_touch_get_xy(&x, &y)) {
            ESP_LOGW(TAG, "Touch detected at (%u, %u) — safe-boot: returning to launcher!", x, y);
            app_return_to_launcher();
            /* Does not return */
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    ESP_LOGI(TAG, "Safe-boot check passed (no touch detected).");
}

/* ─── Read ROM path from NVS ──────────────────────────────────── */
int app_get_rom_path(char *buf, int buflen)
{
    char *path = odroid_settings_RomFilePath_get();
    if (path == NULL || path[0] == '\0') {
        ESP_LOGW(TAG, "No ROM path found in NVS");
        if (path) free(path);
        buf[0] = '\0';
        return -1;
    }
    snprintf(buf, buflen, "%s", path);
    free(path);
    ESP_LOGI(TAG, "ROM path: %s", buf);
    return 0;
}

/* ─── Return to launcher ──────────────────────────────────────── */
void app_return_to_launcher(void)
{
    ESP_LOGI(TAG, "Returning to launcher (factory partition)...");

    /* Black screen + backlight off to avoid white flash during reboot */
    ili9341_clear(0x0000);
    display_flush();
    ili9341_poweroff();
    vTaskDelay(pdMS_TO_TICKS(50));

    const esp_partition_t *factory = esp_partition_find_first(
        ESP_PARTITION_TYPE_APP,
        ESP_PARTITION_SUBTYPE_APP_FACTORY,
        NULL);
    if (factory != NULL) {
        esp_ota_set_boot_partition(factory);
    } else {
        ESP_LOGE(TAG, "Factory partition not found!");
    }

    esp_restart();
    /* Does not return */
    while (1) {}
}
