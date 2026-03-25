/*
 * PSRAM App entry point for OpenTyrian.
 *
 * Receives the service table from the launcher, stores it globally,
 * then starts the game on the current thread with a watchdog task
 * monitoring the MENU button for a clean exit.
 */
#include "psram_app.h"
#include <setjmp.h>
#include <string.h>

/* Global service pointer — used by all SDL shim + syscall code */
const app_services_t *_papp_svc = NULL;

/* Exit mechanism: longjmp back to entry on clean exit request */
static jmp_buf s_exit_jmp;
volatile int papp_exit_requested = 0;

/* Watchdog task handle */
static void *s_wd_handle = NULL;

/*
 * app_return_to_launcher — called from game code (e.g. JE_tyrianHalt)
 * to immediately abort game execution and return to the launcher.
 * Uses longjmp to unwind back to the setjmp in app_entry.
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
                _papp_svc->log_printf("OpenTyrian PAPP: MENU held 3s, requesting exit\n");
                papp_exit_requested = 1;
                return; /* task exits */
            }
        } else {
            held_ms = 0;
        }
        _papp_svc->delay_ms(100);
    }
}

/* OpenTyrian's real main() — defined in opentyr.c */
extern int main(int argc, char *argv[]);

/* Cleanup functions from papp_syscalls.c */
extern void papp_cleanup_fds(void);
extern void papp_cleanup_heap(void);

/* SDL_CloseAudio — from papp_sdl_audio.c */
extern void SDL_CloseAudio(void);

/*
 * app_entry — PSRAM app ABI entry point.
 * Must be placed in .text.entry (offset 0 in the .papp binary).
 */
__attribute__((section(".text.entry")))
int app_entry(const app_services_t *svc)
{
    _papp_svc = svc;
    papp_exit_requested = 0;

    svc->log_printf("=== OpenTyrian PSRAM App Starting ===\n");

    /* Scale 1:1 — SDL_Flip uses display_write_frame_custom(320,200,2.0)
       which does the PPA rotate+scale internally at the right dimensions. */
    svc->display_set_scale(1.0f, 1.0f);

    /* Start watchdog on core 1 */
    svc->task_create(watchdog_task, "ot_wd", 4096, NULL, 1, &s_wd_handle, 1);

    /* Run game on this thread (setjmp for emergency exit) */
    if (setjmp(s_exit_jmp) == 0) {
        char *argv[] = {"opentyrian", NULL};
        main(1, argv);
    }

    svc->log_printf("=== OpenTyrian PSRAM App Exiting ===\n");

    /* ── Comprehensive cleanup ─────────────────────────────────────── */

    /* 1. Kill watchdog task */
    if (s_wd_handle) {
        svc->task_delete(s_wd_handle);
        s_wd_handle = NULL;
    }

    /* 2. Kill audio task + free its buffers (safe even if already closed) */
    SDL_CloseAudio();

    /* 3. Close any file handles still open */
    papp_cleanup_fds();

    /* 4. Free all app heap allocations */
    papp_cleanup_heap();

    /* 5. Clear display before returning to launcher */
    svc->display_clear(0x0000);
    svc->display_flush();

    return 0;
}
