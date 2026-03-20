/*
 * SDL System shim for ESP32-P4
 *
 * Most initialization is handled by app_common (app_init).
 * This provides the SDL API stubs that the game code expects.
 */
#include "SDL_system.h"
#include "SDL_video.h"
#include <stdlib.h>
#include <string.h>

struct SDL_mutex {
    SemaphoreHandle_t sem;
};

void SDL_ClearError(void)
{
}

void SDL_Delay(Uint32 ms)
{
    vTaskDelay(pdMS_TO_TICKS(ms));
}

char *SDL_GetError(void)
{
    return (char *)"";
}

int SDL_Init(Uint32 flags)
{
    if (flags & SDL_INIT_VIDEO)
        SDL_InitSubSystem(SDL_INIT_VIDEO);
    return 0;
}

void SDL_Quit(void)
{
}

void SDL_InitSD(void)
{
    /* SD card already mounted by app_common via app_init() */
    printf("SDL_InitSD: SD already mounted by app_common\n");
}

const SDL_version *SDL_Linked_Version(void)
{
    static SDL_version vers = {SDL_MAJOR_VERSION, SDL_MINOR_VERSION, SDL_PATCHLEVEL};
    return &vers;
}

char ***allocateTwoDimenArrayOnHeapUsingMalloc(int row, int col)
{
    char ***ptr = malloc(row * sizeof(*ptr) + row * (col * sizeof(**ptr)));
    int *const data = (int *const)((char *)ptr + row * sizeof(*ptr));
    for (int i = 0; i < row; i++)
        ptr[i] = (char **)((char *)data + i * col * sizeof(**ptr));
    return ptr;
}

void SDL_DestroyMutex(SDL_mutex *mutex)
{
    if (mutex) {
        if (mutex->sem)
            vSemaphoreDelete(mutex->sem);
        free(mutex);
    }
}

SDL_mutex *SDL_CreateMutex(void)
{
    SDL_mutex *mut = calloc(1, sizeof(SDL_mutex));
    if (mut)
        mut->sem = xSemaphoreCreateMutex();
    return mut;
}

int SDL_LockMutex(SDL_mutex *mutex)
{
    if (mutex && mutex->sem)
        xSemaphoreTake(mutex->sem, portMAX_DELAY);
    return 0;
}

int SDL_UnlockMutex(SDL_mutex *mutex)
{
    if (mutex && mutex->sem)
        xSemaphoreGive(mutex->sem);
    return 0;
}
