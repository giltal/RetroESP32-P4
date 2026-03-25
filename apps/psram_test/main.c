/*
 * PSRAM Test App — Minimal PoC
 *
 * This is the simplest possible PSRAM app. It proves that code loaded
 * from an SD card into PSRAM can execute via MMU mapping and call back
 * into the launcher's services.
 *
 * Build with: tools\build_psram_app.ps1 -AppName psram_test -Sources apps\psram_test\main.c
 * Deploy to:  SD card /sd/apps/psram_test.papp
 */

#define PAPP_APP_SIDE 1
#include "psram_app.h"

/* RGB565 color helpers */
#define RGB565(r, g, b) ((((r) & 0x1F) << 11) | (((g) & 0x3F) << 5) | ((b) & 0x1F))
#define COLOR_RED       RGB565(31, 0, 0)
#define COLOR_GREEN     RGB565(0, 63, 0)
#define COLOR_BLUE      RGB565(0, 0, 31)
#define COLOR_WHITE     RGB565(31, 63, 31)
#define COLOR_BLACK     0x0000

/*
 * Entry point — called by psram_app_loader.
 *
 * Must be placed in .text.entry so the linker puts it at offset 0.
 */
__attribute__((section(".text.entry")))
int app_entry(const app_services_t *svc)
{
    svc->log_printf("=== PSRAM Test App Running! ===\n");
    svc->log_printf("ABI version: %lu\n", (unsigned long)svc->abi_version);
    svc->log_printf("Service table at: %p\n", (const void *)svc);

    /* Verify ABI version */
    if (svc->abi_version != PAPP_ABI_VERSION) {
        svc->log_printf("ERROR: ABI version mismatch\n");
        return -1;
    }

    /* Test 1: Fill screen with red */
    svc->log_printf("Test 1: Red screen...\n");
    svc->display_clear(COLOR_RED);
    svc->display_flush();
    svc->delay_ms(1000);

    /* Test 2: Fill screen with green */
    svc->log_printf("Test 2: Green screen...\n");
    svc->display_clear(COLOR_GREEN);
    svc->display_flush();
    svc->delay_ms(1000);

    /* Test 3: Fill screen with blue */
    svc->log_printf("Test 3: Blue screen...\n");
    svc->display_clear(COLOR_BLUE);
    svc->display_flush();
    svc->delay_ms(1000);

    /* Test 4: Draw gradient pattern using framebuffer */
    svc->log_printf("Test 4: Gradient pattern...\n");
    uint16_t *fb = svc->display_get_framebuffer();
    if (fb) {
        for (int y = 0; y < 480; y++) {
            for (int x = 0; x < 800; x++) {
                uint16_t r = (x * 31) / 800;
                uint16_t g = (y * 63) / 480;
                uint16_t b = 15;
                fb[y * 800 + x] = RGB565(r, g, b);
            }
        }
        svc->display_flush();
        svc->delay_ms(2000);
    } else {
        svc->log_printf("  (framebuffer is NULL, skipping)\n");
    }

    /* Test 5: Read gamepad */
    svc->log_printf("Test 5: Waiting for gamepad input (press A to continue)...\n");
    papp_gamepad_state_t pad;
    for (int timeout = 0; timeout < 50; timeout++) {  /* 5 seconds max */
        svc->input_gamepad_read(&pad);
        if (pad.values[PAPP_INPUT_A]) {
            svc->log_printf("  A button pressed!\n");
            break;
        }
        svc->delay_ms(100);
    }

    /* Test 6: Clean exit */
    svc->log_printf("Test 6: Clearing screen and returning to launcher...\n");
    svc->display_clear(COLOR_BLACK);
    svc->display_flush();
    svc->delay_ms(500);

    svc->log_printf("=== PSRAM Test App Complete ===\n");
    return 0;
}
