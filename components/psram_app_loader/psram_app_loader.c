/*
 * PSRAM App Loader — Implementation
 *
 * Loads .papp binaries from SD card into PSRAM, creates an executable
 * MMU mapping, and calls the app's entry point with a services table.
 *
 * Flow:
 *   1. Read .papp header + code from SD card
 *   2. Allocate page-aligned PSRAM buffer, copy code into it
 *   3. Translate data virtual address → PSRAM physical address
 *   4. esp_mmu_map() to create an executable virtual mapping (PADDR_SHARED)
 *   5. esp_cache_msync() to sync the instruction cache
 *   6. Populate app_services_t function pointer table
 *   7. Call entry_point(&services) — app runs
 *   8. On return: esp_mmu_unmap(), heap_caps_free()
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#include "psram_app.h"

#include "esp_log.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_mmu_map.h"
#include "esp_cache.h"
#include "esp_timer.h"
#include "hal/mmu_types.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/usb_serial_jtag.h"

/* Hardware service headers from existing components */
#include "odroid_display.h"
#include "odroid_audio.h"
#include "odroid_input.h"
#include "odroid_sdcard.h"
#include "odroid_settings.h"

static const char *TAG = "psram_app";

/* MMU page size on ESP32-P4 */
#define MMU_PAGE_SIZE  0x10000   /* 64 KB */
#define ALIGN_UP(x, a) (((x) + (a) - 1) & ~((a) - 1))

/* ── Internal state for a loaded app ─────────────────────────────────── */

struct psram_app {
    /* Loaded binary */
    void    *code_buf;        /* Page-aligned PSRAM buffer (data vaddr)  */
    size_t   code_alloc;      /* Allocated size (page-aligned)           */
    void    *data_buf;        /* .data + .bss buffer (or NULL)           */
    size_t   data_alloc;

    /* Executable mapping */
    void    *exec_ptr;        /* Executable virtual address (MMU mapped) */
    bool     mapped;

    /* From header */
    papp_header_t header;
};

/* ── Service table population ────────────────────────────────────────── */

/* Wrappers for C stdio → void* opaque (service ABI uses void* for FILE*) */
static void *svc_file_open(const char *path, const char *mode) {
    return (void *)fopen(path, mode);
}
static int svc_file_close(void *stream) {
    return fclose((FILE *)stream);
}
static size_t svc_file_read(void *ptr, size_t size, size_t nmemb, void *stream) {
    return fread(ptr, size, nmemb, (FILE *)stream);
}
static size_t svc_file_write(const void *ptr, size_t size, size_t nmemb, void *stream) {
    return fwrite(ptr, size, nmemb, (FILE *)stream);
}
static int svc_file_seek(void *stream, long offset, int whence) {
    return fseek((FILE *)stream, offset, whence);
}
static long svc_file_tell(void *stream) {
    return ftell((FILE *)stream);
}

/* Wrapper: vTaskDelay in milliseconds */
static void svc_delay_ms(int ms) {
    vTaskDelay(pdMS_TO_TICKS(ms));
}

/* Wrapper: esp_timer_get_time → int64_t microseconds */
static int64_t svc_get_time_us(void) {
    return esp_timer_get_time();
}

/* Wrapper: heap_caps_malloc */
static void *svc_mem_caps_alloc(size_t size, uint32_t caps) {
    return heap_caps_malloc(size, caps);
}

/* Wrapper: xTaskCreatePinnedToCore → int return.
 * For large stacks (>32KB), use MALLOC_CAP_SPIRAM so the stack is
 * allocated from PSRAM instead of scarce internal SRAM. */
static int svc_task_create(void (*fn)(void *), const char *name,
                           uint32_t stack_depth, void *arg,
                           int priority, void *out_handle, int core) {
    if (stack_depth > 32768) {
        /* Use heap caps allocation for PSRAM stack */
        TaskHandle_t handle = NULL;
        StackType_t *stack_buf = (StackType_t *)heap_caps_malloc(
            stack_depth, MALLOC_CAP_SPIRAM);
        if (!stack_buf) {
            ESP_LOGE("PAPP", "Failed to alloc %lu bytes PSRAM stack",
                     (unsigned long)stack_depth);
            return -1;
        }
        StaticTask_t *tcb = (StaticTask_t *)heap_caps_malloc(
            sizeof(StaticTask_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        if (!tcb) {
            heap_caps_free(stack_buf);
            ESP_LOGE("PAPP", "Failed to alloc TCB for large-stack task");
            return -1;
        }
        handle = xTaskCreateStaticPinnedToCore(
            fn, name, stack_depth, arg, priority,
            stack_buf, tcb, core);
        if (handle == NULL) {
            heap_caps_free(stack_buf);
            heap_caps_free(tcb);
            return -1;
        }
        if (out_handle) *(TaskHandle_t *)out_handle = handle;
        return 0;
    }
    BaseType_t ret = xTaskCreatePinnedToCore(
        fn, name, stack_depth, arg, priority,
        (TaskHandle_t *)out_handle, core);
    return (ret == pdPASS) ? 0 : -1;
}

/* Wrapper: vTaskDelete */
static void svc_task_delete(void *handle) {
    vTaskDelete((TaskHandle_t)handle);
}

/* Printf via USB JTAG — output goes to COM30 instead of UART0 */
static int svc_jtag_printf(const char *fmt, ...) {
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n > 0)
        usb_serial_jtag_write_bytes(buf, n, pdMS_TO_TICKS(100));
    return n;
}

static int svc_jtag_vprintf(const char *fmt, va_list ap) {
    char buf[256];
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    if (n > 0)
        usb_serial_jtag_write_bytes(buf, n, pdMS_TO_TICKS(100));
    return n;
}

static void populate_services(app_services_t *svc) {
    memset(svc, 0, sizeof(*svc));
    svc->abi_version = PAPP_ABI_VERSION;

    /* Display */
    svc->display_get_framebuffer  = display_get_framebuffer;
    svc->display_get_emu_buffer   = display_get_emu_buffer;
    svc->display_flush            = display_flush;
    svc->display_emu_flush        = display_emu_flush;
    svc->display_clear            = ili9341_clear;
    svc->display_set_scale        = display_set_scale;
    svc->display_write_frame_rgb565 = ili9341_write_frame_rgb565;
    svc->display_write_frame_custom = ili9341_write_frame_rgb565_custom;
    svc->display_write_rect       = ili9341_write_frame_rectangleLE;
    svc->display_lock             = odroid_display_lock;
    svc->display_unlock           = odroid_display_unlock;

    /* Audio */
    svc->audio_init   = odroid_audio_init;
    svc->audio_submit = odroid_audio_submit;

    /* Input — cast is safe: papp_gamepad_state_t has identical layout */
    svc->input_gamepad_read = (void (*)(papp_gamepad_state_t *))odroid_input_gamepad_read;

    /* File I/O */
    svc->file_open  = svc_file_open;
    svc->file_close = svc_file_close;
    svc->file_read  = svc_file_read;
    svc->file_write = svc_file_write;
    svc->file_seek  = svc_file_seek;
    svc->file_tell  = svc_file_tell;

    /* Memory */
    svc->mem_alloc      = malloc;
    svc->mem_calloc     = calloc;
    svc->mem_realloc    = realloc;
    svc->mem_free       = free;
    svc->mem_caps_alloc = svc_mem_caps_alloc;

    /* System — route through USB JTAG for COM30 debug visibility */
    svc->log_printf  = svc_jtag_printf;
    svc->log_vprintf = svc_jtag_vprintf;
    svc->delay_ms    = svc_delay_ms;
    svc->get_time_us = svc_get_time_us;

    /* Settings */
    svc->settings_rom_path_get  = odroid_settings_RomFilePath_get;
    svc->settings_rom_path_set  = odroid_settings_RomFilePath_set;
    svc->settings_volume_get    = odroid_settings_Volume_get;
    svc->settings_volume_set    = odroid_settings_Volume_set;
    svc->settings_brightness_get = odroid_settings_Backlight_get;
    svc->settings_brightness_set = odroid_settings_Backlight_set;

    /* FreeRTOS tasks */
    svc->task_create = svc_task_create;
    svc->task_delete = svc_task_delete;
}

/* ── Loader ──────────────────────────────────────────────────────────── */

esp_err_t psram_app_load(const char *path, psram_app_handle_t *out_handle) {
    ESP_LOGI(TAG, "Loading PSRAM app: %s", path);
    *out_handle = NULL;

    /* Open the .papp file */
    FILE *f = fopen(path, "rb");
    if (!f) {
        ESP_LOGE(TAG, "Failed to open %s", path);
        return ESP_ERR_NOT_FOUND;
    }

    /* Read header */
    papp_header_t hdr;
    if (fread(&hdr, 1, sizeof(hdr), f) != sizeof(hdr)) {
        ESP_LOGE(TAG, "Failed to read header");
        fclose(f);
        return ESP_ERR_INVALID_SIZE;
    }

    /* Validate header */
    if (hdr.magic != PAPP_MAGIC) {
        ESP_LOGE(TAG, "Bad magic: 0x%08lx (expected 0x%08x)",
                 (unsigned long)hdr.magic, PAPP_MAGIC);
        fclose(f);
        return ESP_ERR_INVALID_ARG;
    }
    if (hdr.version != PAPP_ABI_VERSION) {
        ESP_LOGE(TAG, "ABI version mismatch: app=%lu loader=%d",
                 (unsigned long)hdr.version, PAPP_ABI_VERSION);
        fclose(f);
        return ESP_ERR_INVALID_VERSION;
    }
    if (hdr.text_size == 0) {
        ESP_LOGE(TAG, "text_size is 0");
        fclose(f);
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Header OK: text=%lu data=%lu bss=%lu entry_off=%lu",
             (unsigned long)hdr.text_size, (unsigned long)hdr.data_size,
             (unsigned long)hdr.bss_size, (unsigned long)hdr.entry_off);

    /* Allocate handle */
    struct psram_app *app = calloc(1, sizeof(struct psram_app));
    if (!app) {
        fclose(f);
        return ESP_ERR_NO_MEM;
    }
    app->header = hdr;

    /*
     * Allocate a SINGLE contiguous, page-aligned PSRAM buffer for the
     * whole binary image:  [.text+.rodata+.data] [.bss (zeroed)]
     *
     * On ESP32-P4 the I-bus and D-bus share the same virtual address
     * range, so the exec MMU mapping also allows data read/write.
     * Keeping everything contiguous means PC-relative addressing
     * (medany) and absolute initialized pointers both resolve to the
     * correct runtime addresses.
     */
    size_t load_size = hdr.text_size + hdr.data_size;   /* bytes in file */
    size_t total     = load_size + hdr.bss_size;         /* full image   */
    size_t total_aligned = ALIGN_UP(total, MMU_PAGE_SIZE);

    app->code_buf = heap_caps_aligned_alloc(MMU_PAGE_SIZE, total_aligned,
                                            MALLOC_CAP_SPIRAM);
    if (!app->code_buf) {
        ESP_LOGE(TAG, "Failed to allocate %u bytes PSRAM",
                 (unsigned)total_aligned);
        free(app);
        fclose(f);
        return ESP_ERR_NO_MEM;
    }
    app->code_alloc = total_aligned;
    ESP_LOGI(TAG, "App buffer: %p (%u bytes, page-aligned, "
             "load=%u bss=%lu total=%u)",
             app->code_buf, (unsigned)total_aligned,
             (unsigned)load_size, (unsigned long)hdr.bss_size,
             (unsigned)total);

    /* Read loadable image (.text + .rodata + .data) */
    uint32_t _t_read0 = (uint32_t)(esp_timer_get_time() / 1000);
    size_t read_sz = fread(app->code_buf, 1, load_size, f);
    uint32_t _t_read1 = (uint32_t)(esp_timer_get_time() / 1000);
    if (read_sz != load_size) {
        ESP_LOGE(TAG, "Short read: got %u, expected %u",
                 (unsigned)read_sz, (unsigned)load_size);
        heap_caps_free(app->code_buf);
        free(app);
        fclose(f);
        return ESP_ERR_INVALID_SIZE;
    }

    /* Zero .bss + any trailing page padding */
    memset((uint8_t *)app->code_buf + load_size, 0,
           total_aligned - load_size);
    uint32_t _t_bss = (uint32_t)(esp_timer_get_time() / 1000);

    fclose(f);

    /* Write-back entire image from data cache to physical PSRAM so it's
     * visible when later fetched via the instruction-path MMU mapping. */
    esp_cache_msync(app->code_buf, app->code_alloc,
                    ESP_CACHE_MSYNC_FLAG_DIR_C2M
                    | ESP_CACHE_MSYNC_FLAG_TYPE_DATA);
    uint32_t _t_sync = (uint32_t)(esp_timer_get_time() / 1000);

    printf("LOADER TIMING: fread=%lums bss_zero=%lums cache_sync=%lums\n",
           (unsigned long)(_t_read1 - _t_read0),
           (unsigned long)(_t_bss - _t_read1),
           (unsigned long)(_t_sync - _t_bss));

    *out_handle = app;
    ESP_LOGI(TAG, "App loaded successfully");
    return ESP_OK;
}

/* ── Execute ─────────────────────────────────────────────────────────── */

int psram_app_run(psram_app_handle_t handle) {
    if (!handle || !handle->code_buf) {
        ESP_LOGE(TAG, "Invalid handle");
        return -1;
    }

    esp_err_t err;

    /* Step 1: Get physical address of the code buffer */
    esp_paddr_t paddr = 0;
    mmu_target_t target = MMU_TARGET_PSRAM0;
    err = esp_mmu_vaddr_to_paddr(handle->code_buf, &paddr, &target);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "vaddr_to_paddr failed: %s", esp_err_to_name(err));
        return -1;
    }
    ESP_LOGI(TAG, "Code physical address: 0x%08lx", (unsigned long)paddr);

    /* Step 2: Create executable MMU mapping (same physical, new virtual)
     * NOTE: ESP-IDF's s_mem_caps_check() forbids combining EXEC with
     * WRITE or 8BIT, even though P4 PSRAM hardware supports it.
     * Use only EXEC | READ | 32BIT to pass validation. */
    mmu_mem_caps_t exec_caps = MMU_MEM_CAP_EXEC | MMU_MEM_CAP_READ
                             | MMU_MEM_CAP_32BIT;
    err = esp_mmu_map(paddr, handle->code_alloc, MMU_TARGET_PSRAM0,
                      exec_caps, ESP_MMU_MMAP_FLAG_PADDR_SHARED,
                      &handle->exec_ptr);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_mmu_map (exec) failed: %s", esp_err_to_name(err));
        return -1;
    }
    handle->mapped = true;
    ESP_LOGI(TAG, "Executable mapping at: %p", handle->exec_ptr);

    /* Step 3: Sync instruction cache — ensure CPU fetches fresh code */
    err = esp_cache_msync(handle->exec_ptr, handle->code_alloc,
                          ESP_CACHE_MSYNC_FLAG_DIR_M2C
                          | ESP_CACHE_MSYNC_FLAG_TYPE_INST);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "esp_cache_msync returned %s (non-fatal)",
                 esp_err_to_name(err));
    }

    /* Step 4: Populate services table */
    app_services_t svc;
    populate_services(&svc);

    /* Step 5: Compute entry point address */
    papp_entry_fn_t entry = (papp_entry_fn_t)(
        (uint8_t *)handle->exec_ptr + handle->header.entry_off);
    ESP_LOGI(TAG, "Calling entry point at %p ...", (void *)entry);

    /* Step 6: Call the app — blocks until it returns */
    int result = entry(&svc);

    ESP_LOGI(TAG, "App returned with code %d", result);

    /* Step 7: Unmap executable region */
    if (handle->mapped) {
        esp_mmu_unmap(handle->exec_ptr);
        handle->mapped = false;
        handle->exec_ptr = NULL;
    }

    return result;
}

/* ── Unload ──────────────────────────────────────────────────────────── */

void psram_app_unload(psram_app_handle_t handle) {
    if (!handle) return;

    if (handle->mapped && handle->exec_ptr) {
        esp_mmu_unmap(handle->exec_ptr);
    }
    if (handle->code_buf) {
        heap_caps_free(handle->code_buf);
    }
    free(handle);
    ESP_LOGI(TAG, "App unloaded");
}

/* ── Self-Test ───────────────────────────────────────────────────────── */
/*
 * Embeds a tiny RISC-V function (returns 42) to verify PSRAM XIP works
 * without needing an external .papp file.
 *
 * The test function in RISC-V assembly:
 *   addi a0, zero, 42    # return value = 42
 *   ret                   # jalr zero, ra, 0
 */
static const uint8_t selftest_code[] __attribute__((aligned(4))) = {
    0x13, 0x05, 0xa0, 0x02,   /* addi a0, zero, 42 */
    0x67, 0x80, 0x00, 0x00,   /* ret                */
};

esp_err_t psram_app_selftest(void) {
    ESP_LOGI(TAG, "=== PSRAM XIP Self-Test ===");

    /* Allocate page-aligned PSRAM buffer */
    size_t alloc_size = MMU_PAGE_SIZE;  /* Minimum 1 page */
    void *buf = heap_caps_aligned_alloc(MMU_PAGE_SIZE, alloc_size,
                                        MALLOC_CAP_SPIRAM);
    if (!buf) {
        ESP_LOGE(TAG, "SELFTEST: Failed to allocate PSRAM");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "SELFTEST: PSRAM data buffer at %p", buf);

    /* Copy test code into PSRAM */
    memcpy(buf, selftest_code, sizeof(selftest_code));

    /* Write-back data cache so the code is visible in physical PSRAM.
     * Without this, I-cache fetch from the exec mapping reads stale data. */
    esp_cache_msync(buf, alloc_size,
                    ESP_CACHE_MSYNC_FLAG_DIR_C2M
                    | ESP_CACHE_MSYNC_FLAG_TYPE_DATA);

    /* Get physical address */
    esp_paddr_t paddr = 0;
    mmu_target_t target = MMU_TARGET_PSRAM0;
    esp_err_t err = esp_mmu_vaddr_to_paddr(buf, &paddr, &target);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "SELFTEST: vaddr_to_paddr failed: %s",
                 esp_err_to_name(err));
        heap_caps_free(buf);
        return err;
    }
    ESP_LOGI(TAG, "SELFTEST: Physical address: 0x%08lx", (unsigned long)paddr);

    /* Create executable mapping
     * NOTE: Only EXEC | READ | 32BIT — no WRITE or 8BIT per s_mem_caps_check() */
    void *exec_ptr = NULL;
    mmu_mem_caps_t caps = MMU_MEM_CAP_EXEC | MMU_MEM_CAP_READ
                        | MMU_MEM_CAP_32BIT;
    err = esp_mmu_map(paddr, alloc_size, MMU_TARGET_PSRAM0, caps,
                      ESP_MMU_MMAP_FLAG_PADDR_SHARED, &exec_ptr);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "SELFTEST: esp_mmu_map failed: %s",
                 esp_err_to_name(err));
        heap_caps_free(buf);
        return err;
    }
    ESP_LOGI(TAG, "SELFTEST: Executable mapping at %p", exec_ptr);

    /* Sync instruction cache */
    esp_cache_msync(exec_ptr, alloc_size,
                    ESP_CACHE_MSYNC_FLAG_DIR_M2C
                    | ESP_CACHE_MSYNC_FLAG_TYPE_INST);

    /* Call the test function */
    typedef int (*test_fn_t)(void);
    test_fn_t fn = (test_fn_t)exec_ptr;
    ESP_LOGI(TAG, "SELFTEST: Calling function at %p ...", (void *)fn);

    int result = fn();

    ESP_LOGI(TAG, "SELFTEST: Function returned %d (expected 42)", result);

    /* Cleanup */
    esp_mmu_unmap(exec_ptr);
    heap_caps_free(buf);

    if (result == 42) {
        ESP_LOGI(TAG, "=== PSRAM XIP Self-Test PASSED ===");
        return ESP_OK;
    } else {
        ESP_LOGE(TAG, "=== PSRAM XIP Self-Test FAILED (got %d) ===", result);
        return ESP_FAIL;
    }
}
