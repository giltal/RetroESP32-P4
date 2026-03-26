/*
 * PSRAM App entry point for Duke Nukem 3D.
 *
 * Receives the service table from the launcher, stores it globally,
 * then starts the Duke3D engine on a separate task with a watchdog
 * monitoring the MENU button for a clean exit.
 */
#include "psram_app.h"
#include <setjmp.h>
#include <string.h>

/* ── From Duke3D game.c ──────────────────────────────────────────────── */
extern int main(int argc, char **argv);

/* Global service pointer — used by all shim + syscall code */
const app_services_t *_papp_svc = NULL;

/* Exit mechanism: longjmp back to entry on clean exit request */
static jmp_buf s_exit_jmp;
volatile int papp_exit_requested = 0;

/* Task handles */
static void *s_wd_handle = NULL;
static void *s_game_task_handle = NULL;
static volatile int s_game_done = 0;

/* Sound task handle (created by papp_sdl_audio.c) */
extern void papp_sound_shutdown(void);

/*
 * app_return_to_launcher — called from game code or exit path
 * to immediately abort game execution and return to the launcher.
 */
void app_return_to_launcher(void)
{
    papp_exit_requested = 1;
    longjmp(s_exit_jmp, 1);
}

/* Watchdog: polls MENU button, sets exit flag after 3 seconds held */
static void watchdog_task(void *arg)
{
    (void)arg;
    papp_gamepad_state_t state;
    int held_ms = 0;

    for (;;) {
        _papp_svc->input_gamepad_read(&state);
        if (state.values[PAPP_INPUT_MENU]) {
            held_ms += 100;
            if (held_ms >= 3000) {
                _papp_svc->log_printf("Duke3D PAPP: MENU held 3s, requesting exit\n");
                papp_exit_requested = 1;
                while (1) _papp_svc->delay_ms(100);
            }
        } else {
            held_ms = 0;
        }
        _papp_svc->delay_ms(100);
    }
}

/* Cleanup functions from papp_syscalls.c */
extern void papp_cleanup_fds(void);
extern void papp_cleanup_heap(void);

/* Game task: runs Duke3D on a separate task with 256KB stack */
static void duke3d_game_task(void *arg)
{
    (void)arg;
    static char *duke_argv[] = { "duke3d", NULL };

    if (setjmp(s_exit_jmp) == 0) {
        main(1, duke_argv);
    }
    s_game_done = 1;
    /* Suspend self — entry task will delete us */
    while (1) _papp_svc->delay_ms(1000);
}

/*
 * app_entry — PSRAM app ABI entry point.
 * Must be placed in .text.entry (offset 0 in the .papp binary).
 */
__attribute__((section(".text.entry")))
int app_entry(const app_services_t *svc)
{
    _papp_svc = svc;
    papp_exit_requested = 0;
    s_game_done = 0;

    svc->log_printf("=== Duke3D PSRAM App Starting ===\n");

    /* Scale: 320×200 → 2.4× = 768×480 — fills the 480×800 LCD nicely */
    svc->display_set_scale(1.0f, 1.0f);

    /* Start watchdog on core 1 (like Quake) */
    svc->task_create(watchdog_task, "duke_wd", 4096, NULL, 1, &s_wd_handle, 1);

    /* Run game on core 0 with 256KB stack (like Quake).
       CPU1 IDLE WDT is disabled in sdkconfig so core 0 is fine. */
    int rc = svc->task_create(duke3d_game_task, "duke3d", 262144, NULL, 5,
                              &s_game_task_handle, 0);
    if (rc != 0) {
        svc->log_printf("DUKE3D: task_create FAILED (rc=%d)\n", rc);
    } else {
        svc->log_printf("DUKE3D: game task created OK (256KB stack)\n");
    }

    /* Wait for game to finish */
    while (!s_game_done && !papp_exit_requested) {
        svc->delay_ms(100);
    }
    svc->delay_ms(200);

    svc->log_printf("=== Duke3D PSRAM App Exiting ===\n");

    /* ── Comprehensive cleanup ────────────────────────────────────── */

    /* 1. Kill game task */
    if (s_game_task_handle) {
        svc->task_delete(s_game_task_handle);
        s_game_task_handle = NULL;
    }

    /* 2. Kill watchdog task */
    if (s_wd_handle) {
        svc->task_delete(s_wd_handle);
        s_wd_handle = NULL;
    }

    /* 3. Shutdown sound task + free buffers */
    papp_sound_shutdown();

    /* 4. Close any file handles still open */
    papp_cleanup_fds();

    /* 5. Free ALL remaining heap allocations */
    papp_cleanup_heap();

    /* 6. Clear display before returning to launcher */
    svc->display_clear(0x0000);
    svc->display_flush();

    return 0;
}
