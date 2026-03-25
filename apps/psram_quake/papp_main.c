/*
 * PSRAM App entry point for Quake (WinQuake engine).
 *
 * Receives the service table from the launcher, stores it globally,
 * then starts the Quake engine on the current thread with a watchdog
 * task monitoring the MENU button for a clean exit.
 */
#include "psram_app.h"
#include <setjmp.h>
#include <string.h>

/* ── From quake engine ───────────────────────────────────────────────── */
extern void esp32_quake_main(int argc, char **argv, const char *basedir,
                             const char *pakPath, uint32_t pakSize,
                             const void *pakMmap);

/* Global service pointer — used by all shim + syscall code */
const app_services_t *_papp_svc = NULL;

/* Exit mechanism: longjmp back to entry on clean exit request */
static jmp_buf s_exit_jmp;
volatile int papp_exit_requested = 0;

/* Watchdog task handle */
static void *s_wd_handle = NULL;

/* Sound task handle (created by papp_snd.c) */
extern void *quake_sound_task_handle;

/*
 * app_return_to_launcher — called from game code (Sys_Quit, etc.)
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
        _papp_svc->log_printf("QUAKE PANIC: %s\n", msg);
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
                _papp_svc->log_printf("Quake PAPP: MENU held 3s, requesting exit\n");
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

/* Sound shutdown from papp_snd.c */
extern void papp_sound_shutdown(void);

/* Game task: runs Quake on a separate task with a large stack */
static volatile int s_game_done = 0;
static void *s_game_task_handle = NULL;

static void quake_game_task(void *arg)
{
    (void)arg;
    static char *quake_argv[] = { "quake", NULL };

    if (setjmp(s_exit_jmp) == 0) {
        esp32_quake_main(1, quake_argv, "/sd/roms/quake",
                         "/sd/roms/quake/id1/pak0.pak", 0, NULL);
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

    svc->log_printf("=== Quake PSRAM App Starting ===\n");

    /* Scale 1:1 — papp_vid.c uses display_write_frame_custom */
    svc->display_set_scale(1.0f, 1.0f);

    /* Start watchdog on core 1 */
    svc->task_create(watchdog_task, "quake_wd", 4096, NULL, 1, &s_wd_handle, 1);

    /* Run game on a separate task with 256KB stack (WinQuake needs deep stack —
       original quake-go uses 300KB. CL_ParseServerInfo alone has 32KB of locals,
       plus deep BSP traversal during rendering) */
    int rc = svc->task_create(quake_game_task, "quake", 262144, NULL, 5,
                              &s_game_task_handle, 0);
    if (rc != 0) {
        svc->log_printf("QUAKE: task_create FAILED (rc=%d), stack too large?\n", rc);
        /* Fall back to running on this thread */
        quake_game_task(NULL);
    } else {
        svc->log_printf("QUAKE: game task created OK (256KB stack)\n");
    }

    /* Wait for game to finish */
    while (!s_game_done && !papp_exit_requested) {
        svc->delay_ms(100);
    }
    /* Give game task a moment to settle */
    svc->delay_ms(200);

    svc->log_printf("=== Quake PSRAM App Exiting ===\n");

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

    /* 2. Kill sound task + free its buffers */
    papp_sound_shutdown();

    /* 3. Close any file handles still open */
    papp_cleanup_fds();

    /* 4. Free ALL remaining heap allocations */
    papp_cleanup_heap();

    /* 5. Clear display before returning to launcher */
    svc->display_clear(0x0000);
    svc->display_flush();

    return 0;
}
