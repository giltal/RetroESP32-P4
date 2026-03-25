/*
 * SDL Event/Input shim for PSRAM App — routes through app_services_t.
 * Replaces components/opentyrian_sdl/SDL_event.c
 *
 * Injects SDL_QUIT when the launcher watchdog sets papp_exit_requested.
 */
#include "psram_app.h"
#include "SDL_event.h"
#include <string.h>

extern const app_services_t *_papp_svc;
extern volatile int papp_exit_requested;

typedef struct {
    int papp_id;
    SDL_Scancode scancode;
    SDL_Keycode keycode;
} InputKeyMap;

static const InputKeyMap keymap[] = {
    {PAPP_INPUT_UP,     SDL_SCANCODE_UP,     SDLK_UP},
    {PAPP_INPUT_DOWN,   SDL_SCANCODE_DOWN,   SDLK_DOWN},
    {PAPP_INPUT_LEFT,   SDL_SCANCODE_LEFT,   SDLK_LEFT},
    {PAPP_INPUT_RIGHT,  SDL_SCANCODE_RIGHT,  SDLK_RIGHT},
    {PAPP_INPUT_A,      SDL_SCANCODE_SPACE,  SDLK_SPACE},
    {PAPP_INPUT_B,      SDL_SCANCODE_RETURN, SDLK_RETURN},
    {PAPP_INPUT_START,  SDL_SCANCODE_LALT,   SDLK_LALT},
    {PAPP_INPUT_SELECT, SDL_SCANCODE_LCTRL,  SDLK_LCTRL},
    {PAPP_INPUT_MENU,   SDL_SCANCODE_ESCAPE, SDLK_ESCAPE},
};

#define NUM_KEYS (sizeof(keymap) / sizeof(keymap[0]))

static int last_state[PAPP_INPUT_MAX] = {0};
static int input_initialized = 0;

int SDL_PollEvent(SDL_Event *event)
{
    if (!input_initialized) {
        memset(last_state, 0, sizeof(last_state));
        input_initialized = 1;
    }

    /* If exit was requested, inject QUIT event */
    if (papp_exit_requested) {
        event->key.type = SDL_QUIT;
        return 1;
    }

    papp_gamepad_state_t state;
    _papp_svc->input_gamepad_read(&state);

    event->key.keysym.mod = 0;

    for (int i = 0; i < (int)NUM_KEYS; i++) {
        int id = keymap[i].papp_id;
        int cur = state.values[id];
        if (cur != last_state[id]) {
            last_state[id] = cur;
            event->key.keysym.scancode = keymap[i].scancode;
            event->key.keysym.sym = keymap[i].keycode;
            event->key.type = cur ? SDL_KEYDOWN : SDL_KEYUP;
            event->key.state = cur ? SDL_PRESSED : SDL_RELEASED;
            return 1;
        }
    }

    return 0;
}

void inputInit(void)
{
    input_initialized = 1;
}
