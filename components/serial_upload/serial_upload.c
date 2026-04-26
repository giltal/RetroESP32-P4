/*
 * Serial Upload — receive files over USB Serial JTAG and write to SD card.
 *
 * Protocol (all text-based header, then raw binary data):
 *   PC → ESP:  "PAPU"               (4-byte magic)
 *   PC → ESP:  "<path>\n"           (destination path, e.g. /sd/roms/papp/foo.papp)
 *   PC → ESP:  "<size>\n"           (decimal file size in bytes)
 *   ESP → PC:  "\x06READY\n"       (ready to receive, \x06 = ACK prefix)
 *   PC → ESP:  <raw binary data>   (exactly <size> bytes)
 *   ESP → PC:  "\x06OK <size>\n"   (success) or "\x06ERR:<msg>\n" (failure)
 *
 * The \x06 prefix lets the PC-side script distinguish protocol responses
 * from interleaved ESP_LOG output.
 */

#include "serial_upload.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/usb_serial_jtag.h"
#include "esp_log.h"

static const char *TAG = "serial_upload";
static TaskHandle_t s_upload_task = NULL;

#define MAGIC       "PAPU"
#define MAGIC_LEN   4
#define CHUNK_SIZE  4096
#define MAX_PATH    256
#define MAX_FILE_SIZE (48 * 1024 * 1024)  /* 48 MB — large enough for Neo Geo sprite caches */

/* ── Helpers ─────────────────────────────────────────────────────────── */

/* Read exactly `len` bytes, return 0 on success, -1 on timeout. */
static int read_exact(uint8_t *buf, size_t len, int timeout_ms)
{
    size_t got = 0;
    TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(timeout_ms);
    while (got < len) {
        TickType_t now = xTaskGetTickCount();
        if ((int32_t)(now - deadline) >= 0) return -1;
        TickType_t remaining = deadline - now;
        int n = usb_serial_jtag_read_bytes(buf + got, len - got, remaining);
        if (n > 0) got += n;
    }
    return 0;
}

/* Read until '\n', null-terminate, return length (excl. NUL), -1 on timeout. */
static int read_line(char *buf, size_t max, int timeout_ms)
{
    size_t pos = 0;
    TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(timeout_ms);
    while (pos < max - 1) {
        TickType_t now = xTaskGetTickCount();
        if ((int32_t)(now - deadline) >= 0) return -1;
        TickType_t remaining = deadline - now;
        uint8_t c;
        int n = usb_serial_jtag_read_bytes(&c, 1, remaining);
        if (n <= 0) continue;
        if (c == '\n') { buf[pos] = '\0'; return (int)pos; }
        if (c != '\r') buf[pos++] = (char)c;
    }
    buf[pos] = '\0';
    return (int)pos;
}

/* Send a protocol response (prefixed with \x06 so Python can find it). */
static void send_response(const char *msg)
{
    const char prefix = '\x06';
    usb_serial_jtag_write_bytes(&prefix, 1, pdMS_TO_TICKS(100));
    usb_serial_jtag_write_bytes(msg, strlen(msg), pdMS_TO_TICKS(1000));
}

/* Recursively create directories for a given file path (like mkdir -p). */
static void ensure_parent_dirs(const char *filepath)
{
    char tmp[MAX_PATH];
    strncpy(tmp, filepath, sizeof(tmp) - 1);
    tmp[sizeof(tmp) - 1] = '\0';

    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            mkdir(tmp, 0755);
            *p = '/';
        }
    }
}

/* ── Upload handler ──────────────────────────────────────────────────── */

static void handle_upload(void)
{
    char path[MAX_PATH];
    char size_str[32];

    ESP_LOGI(TAG, "Upload magic received");

    /* Read destination path */
    if (read_line(path, sizeof(path), 5000) <= 0) {
        send_response("ERR:timeout reading path\n");
        return;
    }

    /* Read file size */
    if (read_line(size_str, sizeof(size_str), 5000) <= 0) {
        send_response("ERR:timeout reading size\n");
        return;
    }

    uint32_t file_size = (uint32_t)strtoul(size_str, NULL, 10);
    if (file_size == 0 || file_size > MAX_FILE_SIZE) {
        send_response("ERR:invalid size\n");
        return;
    }

    ESP_LOGI(TAG, "Upload: '%s' (%lu bytes)", path, (unsigned long)file_size);

    /* Ensure parent directories exist */
    ensure_parent_dirs(path);

    /* Open file for writing */
    FILE *f = fopen(path, "wb");
    if (!f) {
        ESP_LOGE(TAG, "Cannot open '%s' for writing", path);
        send_response("ERR:cannot open file\n");
        return;
    }

    send_response("READY\n");

    /* Receive file data in chunks with flow control.
       After writing each chunk to SD, we send back '\x06' so the PC
       knows it's safe to send the next chunk.  Without this, USB
       Serial JTAG's 16 KB ring buffer overflows at high speed. */
    uint8_t *chunk = malloc(CHUNK_SIZE);
    if (!chunk) {
        fclose(f);
        remove(path);
        send_response("ERR:no memory\n");
        return;
    }

    uint32_t remaining = file_size;
    uint32_t written = 0;
    const char ack_byte = '\x06';

    while (remaining > 0) {
        size_t to_read = (remaining > CHUNK_SIZE) ? CHUNK_SIZE : remaining;
        if (read_exact(chunk, to_read, 30000) < 0) {
            ESP_LOGE(TAG, "Timeout at byte %lu/%lu",
                     (unsigned long)written, (unsigned long)file_size);
            free(chunk);
            fclose(f);
            remove(path);
            send_response("ERR:timeout during transfer\n");
            return;
        }
        size_t w = fwrite(chunk, 1, to_read, f);
        if (w != to_read) {
            ESP_LOGE(TAG, "Write error at byte %lu", (unsigned long)written);
            free(chunk);
            fclose(f);
            remove(path);
            send_response("ERR:write failed\n");
            return;
        }
        remaining -= to_read;
        written += to_read;

        /* Chunk ACK — tell PC to send next chunk */
        usb_serial_jtag_write_bytes(&ack_byte, 1, pdMS_TO_TICKS(100));
    }

    free(chunk);
    fclose(f);

    char resp[64];
    snprintf(resp, sizeof(resp), "OK %lu\n", (unsigned long)written);
    send_response(resp);

    ESP_LOGI(TAG, "Upload complete: %lu bytes → %s",
             (unsigned long)written, path);
}

/* ── Background task ─────────────────────────────────────────────────── */

static void serial_upload_task(void *arg)
{
    ESP_LOGI(TAG, "Listening for upload on USB Serial JTAG...");

    const uint8_t magic[] = MAGIC;
    int idx = 0;

    for (;;) {
        uint8_t byte;
        int n = usb_serial_jtag_read_bytes(&byte, 1, pdMS_TO_TICKS(500));
        if (n <= 0) continue;

        if (byte == magic[idx]) {
            idx++;
            if (idx == MAGIC_LEN) {
                handle_upload();
                idx = 0;
            }
        } else {
            idx = (byte == magic[0]) ? 1 : 0;
        }
    }
}

/* ── Public API ──────────────────────────────────────────────────────── */

void serial_upload_init(void)
{
    /* Install the USB Serial JTAG interrupt-driven driver with generous
       RX/TX buffers so file data doesn't get dropped. */
    usb_serial_jtag_driver_config_t cfg = {
        .rx_buffer_size = 16384,
        .tx_buffer_size = 4096,
    };
    esp_err_t err = usb_serial_jtag_driver_install(&cfg);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "USB Serial JTAG driver installed (rx=%d tx=%d)",
                 (int)cfg.rx_buffer_size, (int)cfg.tx_buffer_size);
    } else {
        ESP_LOGW(TAG, "USB Serial JTAG driver install: %s (may already be active)",
                 esp_err_to_name(err));
    }

    /* Note: USB Serial JTAG secondary console is disabled in sdkconfig
     * (CONFIG_ESP_CONSOLE_SECONDARY_NONE=y) to prevent 50ms-per-char
     * stalls from ESP_LOG when the USB host isn't reading.
     * Serial upload still works via usb_serial_jtag_read/write_bytes(). */

    xTaskCreatePinnedToCore(serial_upload_task, "serial_upload",
                            8192, NULL, 3, &s_upload_task, 1);
}

void serial_upload_deinit(void)
{
    if (s_upload_task) {
        vTaskDelete(s_upload_task);
        s_upload_task = NULL;
        /* Small delay so the deleted task's stack/TCB are freed */
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    usb_serial_jtag_driver_uninstall();
}
