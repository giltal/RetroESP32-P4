/*
 * SDL Event/Input shim for Duke3D PSRAM App.
 *
 * Duke3D's display.c calls SDL_PollEvent() in _handle_events().
 * Events go to root_sdl_event_filter() → sdl_key_filter() which
 * looks up scancodes[event->key.keysym.sym] to set lastkey and
 * calls keyhandler() to update keystatus[].
 *
 * We read the gamepad via the papp service table and generate
 * edge-triggered SDL key events with appropriate SDLK_* values
 * that the engine's scancode table already understands.
 *
 * Gamepad mapping for Duke3D:
 *   D-pad        → Arrow keys (move/turn)
 *   A button     → LCTRL (fire)
 *   B button     → Space (open doors/switches)
 *   X button     → A key (jump)
 *   Y button     → Z key (crouch)
 *   L shoulder   → previous weapon (cycles number keys 1-0)
 *   R shoulder   → next weapon (cycles number keys 1-0)
 *   Start        → Escape (menu)
 *   Select       → Tab (map toggle)
 *   MENU (3s)    → watchdog handles exit
 */
#include "psram_app.h"
#include "SDL.h"
#include <string.h>

extern const app_services_t *_papp_svc;
extern volatile int papp_exit_requested;

/* keyMode: 0 = normal, 1 = menu mode. Used by menues.c */
int keyMode = 0;

typedef struct {
    int papp_id;      /* PAPP_INPUT_xxx */
    SDLKey sdlkey;    /* SDLK_xxx for the scancode table */
} PadKeyMap;

static const PadKeyMap keymap[] = {
    { PAPP_INPUT_UP,      SDLK_UP     },
    { PAPP_INPUT_DOWN,    SDLK_DOWN   },
    { PAPP_INPUT_LEFT,    SDLK_LEFT   },
    { PAPP_INPUT_RIGHT,   SDLK_RIGHT  },
    { PAPP_INPUT_A,       SDLK_LCTRL  },  /* fire */
    { PAPP_INPUT_B,       SDLK_SPACE  },  /* open doors/switches */
    { PAPP_INPUT_X,       SDLK_a      },  /* jump */
    { PAPP_INPUT_Y,       SDLK_z      },  /* crouch */
    /* L/R handled separately for weapon cycling */
    { PAPP_INPUT_START,   SDLK_ESCAPE },  /* menu */
    { PAPP_INPUT_SELECT,  SDLK_TAB    },  /* automap */
};

#define NUM_MAP (sizeof(keymap) / sizeof(keymap[0]))

/* Weapon cycling state for L/R shoulders.
 * Cycles through weapon slots 1-10 via number keys 1-9,0.
 * Weapons the player doesn't have are skipped by the engine. */
static int s_weapon_slot = 1;  /* 1-10, maps to keys 1-9,0 */
static SDLKey s_l_held_key = SDLK_UNKNOWN;  /* key held down by L */
static SDLKey s_r_held_key = SDLK_UNKNOWN;  /* key held down by R */

static SDLKey weapon_slot_to_key(int slot)
{
    if (slot >= 1 && slot <= 9) return (SDLKey)(SDLK_0 + slot);
    if (slot == 10) return SDLK_0;
    return SDLK_1;
}

static int s_prev_state[PAPP_INPUT_MAX];
static int s_event_queue_head = 0;
static int s_event_queue_tail = 0;

/* Small ring buffer for multi-button frames */
#define EVQ_SIZE 32
static SDL_Event s_evq[EVQ_SIZE];

static void evq_push(const SDL_Event *ev)
{
    int next = (s_event_queue_head + 1) % EVQ_SIZE;
    if (next == s_event_queue_tail) return; /* full, drop */
    s_evq[s_event_queue_head] = *ev;
    s_event_queue_head = next;
}

static int evq_pop(SDL_Event *ev)
{
    if (s_event_queue_tail == s_event_queue_head) return 0;
    *ev = s_evq[s_event_queue_tail];
    s_event_queue_tail = (s_event_queue_tail + 1) % EVQ_SIZE;
    return 1;
}

static int evq_empty(void)
{
    return s_event_queue_tail == s_event_queue_head;
}

static void push_key_event(SDLKey sym, int pressed)
{
    SDL_Event ev;
    memset(&ev, 0, sizeof(ev));
    ev.key.type  = pressed ? SDL_KEYDOWN : SDL_KEYUP;
    ev.key.state = pressed ? SDL_PRESSED : SDL_RELEASED;
    ev.key.keysym.sym = sym;
    ev.key.keysym.mod = 0;
    evq_push(&ev);
}

static void poll_gamepad(void)
{
    papp_gamepad_state_t state;
    _papp_svc->input_gamepad_read(&state);

    /* Standard button mappings */
    for (int i = 0; i < (int)NUM_MAP; i++) {
        int id = keymap[i].papp_id;
        int cur = state.values[id];
        if (cur != s_prev_state[id]) {
            s_prev_state[id] = cur;
            push_key_event(keymap[i].sdlkey, cur);
        }
    }

    /* L shoulder = previous weapon: KEYDOWN on press, KEYUP on release */
    {
        int cur = state.values[PAPP_INPUT_L];
        if (cur && !s_prev_state[PAPP_INPUT_L]) {
            /* Button just pressed — cycle backward, send KEYDOWN */
            s_weapon_slot--;
            if (s_weapon_slot < 1) s_weapon_slot = 10;
            s_l_held_key = weapon_slot_to_key(s_weapon_slot);
            push_key_event(s_l_held_key, 1);
        } else if (!cur && s_prev_state[PAPP_INPUT_L]) {
            /* Button released — send KEYUP for the key we pressed */
            if (s_l_held_key != SDLK_UNKNOWN) {
                push_key_event(s_l_held_key, 0);
                s_l_held_key = SDLK_UNKNOWN;
            }
        }
        s_prev_state[PAPP_INPUT_L] = cur;
    }

    /* R shoulder = next weapon: KEYDOWN on press, KEYUP on release */
    {
        int cur = state.values[PAPP_INPUT_R];
        if (cur && !s_prev_state[PAPP_INPUT_R]) {
            /* Button just pressed — cycle forward, send KEYDOWN */
            s_weapon_slot++;
            if (s_weapon_slot > 10) s_weapon_slot = 1;
            s_r_held_key = weapon_slot_to_key(s_weapon_slot);
            push_key_event(s_r_held_key, 1);
        } else if (!cur && s_prev_state[PAPP_INPUT_R]) {
            /* Button released — send KEYUP for the key we pressed */
            if (s_r_held_key != SDLK_UNKNOWN) {
                push_key_event(s_r_held_key, 0);
                s_r_held_key = SDLK_UNKNOWN;
            }
        }
        s_prev_state[PAPP_INPUT_R] = cur;
    }
}

int SDL_PollEvent(SDL_Event *event)
{
    /* Inject QUIT if exit requested */
    if (papp_exit_requested) {
        memset(event, 0, sizeof(*event));
        event->type = SDL_QUIT;
        return 1;
    }

    /* If queue is empty, poll gamepad to refill */
    if (evq_empty())
        poll_gamepad();

    /* Return one event from the queue */
    return evq_pop(event);
}

/* Duke3D's display.c also calls these: */
int SDL_NumJoysticks(void) { return 0; }
const char *SDL_JoystickName(int device_index) { return "none"; }
SDL_Joystick *SDL_JoystickOpen(int device_index) { return NULL; }
void SDL_JoystickClose(SDL_Joystick *joystick) {}
int SDL_JoystickNumAxes(SDL_Joystick *joystick) { return 0; }
int SDL_JoystickNumButtons(SDL_Joystick *joystick) { return 0; }
int SDL_JoystickNumHats(SDL_Joystick *joystick) { return 0; }
int SDL_JoystickNumBalls(SDL_Joystick *joystick) { return 0; }
void SDL_JoystickUpdate(void) {}
Sint16 SDL_JoystickGetAxis(SDL_Joystick *joystick, int axis) { return 0; }
Uint8 SDL_JoystickGetHat(SDL_Joystick *joystick, int hat) { return 0; }
Uint8 SDL_JoystickGetButton(SDL_Joystick *joystick, int button) { return 0; }

/* Networking stubs — no networking on handheld */
void Setup_UnstableNetworking(void) {}
void Setup_StableNetworking(void) {}

/* Mouse stubs — no mouse on handheld */
Uint8 SDL_GetMouseState(int *x, int *y)
{
    if (x) *x = 0;
    if (y) *y = 0;
    return 0;
}

Uint8 SDL_GetRelativeMouseState(int *x, int *y)
{
    if (x) *x = 0;
    if (y) *y = 0;
    return 0;
}

void SDL_WarpMouse(Uint16 x, Uint16 y) {}
void SDL_SetModState(SDL_Keymod modstate) {}

/* Keyboard state — Duke3D doesn't call this much, BUILD uses keystatus[] */
Uint8 *SDL_GetKeyState(int *numkeys)
{
    static Uint8 keystate[512];
    if (numkeys) *numkeys = 512;
    return keystate;
}

char *SDL_GetKeyName(SDLKey key)
{
    return (char *)"";
}

SDL_Keymod SDL_GetModState(void)
{
    return (SDL_Keymod)0;
}
