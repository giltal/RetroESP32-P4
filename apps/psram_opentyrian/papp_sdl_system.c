/*
 * SDL System shim for PSRAM App — routes through app_services_t.
 * Replaces components/opentyrian_sdl/SDL_system.c
 */
#include "psram_app.h"
#include "SDL_system.h"
#include "SDL_video.h"
#include <stdlib.h>
#include <string.h>

extern const app_services_t *_papp_svc;

struct SDL_mutex {
    volatile int locked;
};

void SDL_ClearError(void) {}

void SDL_Delay(Uint32 ms)
{
    _papp_svc->delay_ms((int)ms);
}

char *SDL_GetError(void) { return (char *)""; }

int SDL_Init(Uint32 flags)
{
    if (flags & SDL_INIT_VIDEO)
        SDL_InitSubSystem(SDL_INIT_VIDEO);
    return 0;
}

void SDL_Quit(void) {}

void SDL_InitSD(void)
{
    /* SD already mounted by launcher */
}

const SDL_version *SDL_Linked_Version(void)
{
    static SDL_version vers = {SDL_MAJOR_VERSION, SDL_MINOR_VERSION, SDL_PATCHLEVEL};
    return &vers;
}

char ***allocateTwoDimenArrayOnHeapUsingMalloc(int row, int col)
{
    char ***ptr = _papp_svc->mem_alloc(row * sizeof(*ptr) + row * (col * sizeof(**ptr)));
    int *const data = (int *const)((char *)ptr + row * sizeof(*ptr));
    for (int i = 0; i < row; i++)
        ptr[i] = (char **)((char *)data + i * col * sizeof(**ptr));
    return ptr;
}

void SDL_DestroyMutex(SDL_mutex *mutex)
{
    if (mutex)
        _papp_svc->mem_free(mutex);
}

SDL_mutex *SDL_CreateMutex(void)
{
    SDL_mutex *mut = _papp_svc->mem_calloc(1, sizeof(SDL_mutex));
    return mut;
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
