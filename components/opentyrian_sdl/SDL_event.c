/*
 * SDL Event/Input shim for ESP32-P4
 *
 * Replaces GPIO/ADC-based input with USB HID gamepad via odroid_input.
 * Button mapping:
 *   UP/DOWN/LEFT/RIGHT → arrow keys
 *   A     → SPACE (fire)
 *   B     → RETURN (enter/confirm)
 *   START → LALT  (game start/pause)
 *   SELECT→ LCTRL (select/alt fire)
 *   MENU  → ESCAPE (quit/back)
 */
#include "SDL_event.h"
#include "odroid_input.h"
#include <string.h>

typedef struct {
    int input_id;
    SDL_Scancode scancode;
    SDL_Keycode keycode;
} InputKeyMap;

static const InputKeyMap keymap[] = {
    {ODROID_INPUT_UP,     SDL_SCANCODE_UP,     SDLK_UP},
    {ODROID_INPUT_DOWN,   SDL_SCANCODE_DOWN,   SDLK_DOWN},
    {ODROID_INPUT_LEFT,   SDL_SCANCODE_LEFT,   SDLK_LEFT},
    {ODROID_INPUT_RIGHT,  SDL_SCANCODE_RIGHT,  SDLK_RIGHT},
    {ODROID_INPUT_A,      SDL_SCANCODE_SPACE,  SDLK_SPACE},
    {ODROID_INPUT_B,      SDL_SCANCODE_RETURN, SDLK_RETURN},
    {ODROID_INPUT_START,  SDL_SCANCODE_LALT,   SDLK_LALT},
    {ODROID_INPUT_SELECT, SDL_SCANCODE_LCTRL,  SDLK_LCTRL},
    {ODROID_INPUT_MENU,   SDL_SCANCODE_ESCAPE, SDLK_ESCAPE},
};

#define NUM_KEYS (sizeof(keymap) / sizeof(keymap[0]))

static int last_state[ODROID_INPUT_MAX] = {0};
static bool input_initialized = false;

int SDL_PollEvent(SDL_Event *event)
{
    if (!input_initialized) {
        /* odroid_input_gamepad_init() already called by app_init() */
        memset(last_state, 0, sizeof(last_state));
        input_initialized = true;
    }

    odroid_gamepad_state state;
    odroid_input_gamepad_read(&state);

    event->key.keysym.mod = 0;

    for (int i = 0; i < NUM_KEYS; i++) {
        int id = keymap[i].input_id;
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
    /* No-op — gamepad already initialized by app_common */
    input_initialized = true;
}
