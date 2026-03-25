/*
 * PSRAM App entry point for Doom (prboom-go).
 *
 * Receives the service table from the launcher, stores it globally,
 * then starts the Doom engine on the current thread with a watchdog
 * task monitoring the MENU button for a clean exit.
 */
#include "psram_app.h"
#include <setjmp.h>
#include <string.h>

/* ── From prboom engine ──────────────────────────────────────────────── */
extern int SCREENWIDTH;
extern int SCREENHEIGHT;
extern char **myargv;
extern int myargc;
extern void Z_Init(void);
extern void D_DoomMain(void);

/* Global service pointer — used by all shim + syscall code */
const app_services_t *_papp_svc = NULL;

/* Exit mechanism: longjmp back to entry on clean exit request */
static jmp_buf s_exit_jmp;
volatile int papp_exit_requested = 0;

/* Watchdog task handle */
static void *s_wd_handle = NULL;

/* Sound task handle (created by papp_i_sound.c) */
extern void *doom_sound_task_handle;

/*
 * app_return_to_launcher — called from game code (I_SafeExit, etc.)
 * to immediately abort game execution and return to the launcher.
 */
void app_return_to_launcher(void)
{
    papp_exit_requested = 1;
    longjmp(s_exit_jmp, 1);
}

/* Called by RG_PANIC macro in rg_system.h stub */
void papp_panic(const char *msg)
{
    if (_papp_svc)
        _papp_svc->log_printf("DOOM PANIC: %s\n", msg);
    app_return_to_launcher();
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
                _papp_svc->log_printf("Doom PAPP: MENU held 3s, requesting exit\n");
                papp_exit_requested = 1;
                /* Do NOT return — returning from a FreeRTOS task function
                   calls prvTaskExitError() → abort() → reboot.  Spin here
                   until the cleanup path deletes this task. */
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

/* Sound shutdown from papp_i_sound.c */
extern void papp_sound_shutdown(void);

/*
 * app_entry — PSRAM app ABI entry point.
 * Must be placed in .text.entry (offset 0 in the .papp binary).
 */
__attribute__((section(".text.entry")))
int app_entry(const app_services_t *svc)
{
    _papp_svc = svc;
    papp_exit_requested = 0;

    svc->log_printf("=== Doom PSRAM App Starting ===\n");

    /* Scale 1:1 — papp_i_video uses display_write_frame_custom with 2.4f */
    svc->display_set_scale(1.0f, 1.0f);

    /* Start watchdog on core 1 */
    svc->task_create(watchdog_task, "doom_wd", 4096, NULL, 1, &s_wd_handle, 1);

    /* Set up Doom screen dimensions */
    SCREENWIDTH  = 320;
    SCREENHEIGHT = 200;

    /* Set up Doom command-line arguments */
    static char *doom_argv[] = {
        "doom",
        "-save", "/sd/saves/doom",
        "-iwad", "/sd/roms/doom/doom.wad",
        NULL
    };
    myargv = doom_argv;
    myargc = 5;

    /* Run game on this thread (setjmp for emergency exit) */
    if (setjmp(s_exit_jmp) == 0) {
        Z_Init();
        D_DoomMain();   /* never returns normally — runs game loop */
    }

    svc->log_printf("=== Doom PSRAM App Exiting ===\n");

    /* ── Comprehensive cleanup ────────────────────────────────────── */

    /* 1. Kill watchdog task */
    if (s_wd_handle) {
        svc->task_delete(s_wd_handle);
        s_wd_handle = NULL;
    }

    /* 2. Kill sound task + free its buffers */
    papp_sound_shutdown();

    /* 3. Close any file handles still open */
    papp_cleanup_fds();

    /* 4. Free ALL remaining heap allocations (zone memory, WAD data, etc.) */
    papp_cleanup_heap();

    /* 5. Clear display before returning to launcher */
    svc->display_clear(0x0000);
    svc->display_flush();

    return 0;
}
