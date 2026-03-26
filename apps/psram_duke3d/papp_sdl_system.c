/*
 * SDL System shim for Duke3D PSRAM App.
 *
 * Provides timer, init/quit, thread, and misc SDL functions
 * that the engine relies on.
 */
#include "psram_app.h"
#include "SDL.h"
#include <string.h>
#include <stdlib.h>

extern const app_services_t *_papp_svc;
extern volatile int papp_exit_requested;

/* ── Timer ───────────────────────────────────────────────────────────── */

Uint32 SDL_GetTicks(void)
{
    return (Uint32)(_papp_svc->get_time_us() / 1000);
}

void SDL_Delay(Uint32 ms)
{
    /* Ensure at least 1ms yield to feed watchdog */
    _papp_svc->delay_ms(ms > 0 ? (int)ms : 1);
}

/* Duke3D's display.c uses getticks() which is defined in the engine.
 * It may also call SDL_GetTicks directly. */

/* ── Init / Quit ─────────────────────────────────────────────────────── */

int SDL_Init(Uint32 flags)
{
    return 0; /* All subsystems "initialized" immediately */
}

void SDL_Quit(void)
{
    /* Nothing to clean up — papp_main handles everything */
}

Uint32 SDL_WasInit(Uint32 flags)
{
    return flags; /* pretend everything is initialized */
}

/* ── Error handling ──────────────────────────────────────────────────── */

static char s_error_buf[256] = "";

void SDL_ClearError(void) { s_error_buf[0] = '\0'; }
char *SDL_GetError(void) { return s_error_buf; }

int SDL_SetError(const char *fmt, ...)
{
    /* Just store a generic error message */
    strncpy(s_error_buf, "SDL error", sizeof(s_error_buf) - 1);
    return -1;
}

/* ── Mutex ───────────────────────────────────────────────────────────── */

struct SDL_mutex {
    volatile int locked;
};

SDL_mutex *SDL_CreateMutex(void)
{
    SDL_mutex *m = _papp_svc->mem_calloc(1, sizeof(SDL_mutex));
    return m;
}

void SDL_DestroyMutex(SDL_mutex *mutex)
{
    if (mutex) _papp_svc->mem_free(mutex);
}

int SDL_LockMutex(SDL_mutex *mutex)
{
    if (mutex) {
        while (__atomic_exchange_n(&mutex->locked, 1, __ATOMIC_ACQUIRE))
            _papp_svc->delay_ms(1);
    }
    return 0;
}

int SDL_UnlockMutex(SDL_mutex *mutex)
{
    if (mutex)
        __atomic_store_n(&mutex->locked, 0, __ATOMIC_RELEASE);
    return 0;
}

/* ── SDL_RWops — minimal for SDL_SaveBMP which we stub ───────────────── */

SDL_RWops *SDL_RWFromMem(void *mem, int size)
{
    return NULL; /* Not used */
}

/* ── Misc stubs ──────────────────────────────────────────────────────── */

const SDL_version *SDL_Linked_Version(void)
{
    static SDL_version v = { SDL_MAJOR_VERSION, SDL_MINOR_VERSION, SDL_PATCHLEVEL };
    return &v;
}

/* putenv / getenv — used by display.c for driver config */
int SDL_putenv(const char *var) { return 0; }

/* TIMER functions are in Engine/display.c — they use SDL_GetTicks
 * which routes through our shim above. No need to duplicate here. */
