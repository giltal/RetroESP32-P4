/*
 * Doom I_System + I_StartTic shim for PSRAM App.
 *
 * System timing, input polling, and exit handling routed through
 * the app_services_t service table.
 */
#include "psram_app.h"
#include <string.h>
#include <stdint.h>

/* prboom engine headers */
#include <doomdef.h>
#include <doomstat.h>
#include <d_event.h>
#include <g_game.h>
#include <i_main.h>
#include <i_system.h>
#include <r_fps.h>
#include <s_sound.h>

extern const app_services_t *_papp_svc;
extern volatile int papp_exit_requested;
extern void app_return_to_launcher(void);

/* ── Input keymap ────────────────────────────────────────────────────── */

/* Map PAPP_INPUT_* bitmask positions to Doom key globals.
 * The retro-go version used bitmask keys; we use the papp_gamepad array. */
typedef struct {
    int papp_id;    /* PAPP_INPUT_* index */
    int *key;       /* pointer to Doom key variable */
} doom_keymap_t;

static const doom_keymap_t keymap[] = {
    {PAPP_INPUT_UP,     &key_up},
    {PAPP_INPUT_DOWN,   &key_down},
    {PAPP_INPUT_LEFT,   &key_left},
    {PAPP_INPUT_RIGHT,  &key_right},
    {PAPP_INPUT_A,      &key_fire},
    {PAPP_INPUT_A,      &key_enter},
    {PAPP_INPUT_B,      &key_speed},
    {PAPP_INPUT_B,      &key_strafe},
    {PAPP_INPUT_B,      &key_backspace},
    {PAPP_INPUT_MENU,   &key_escape},
    {PAPP_INPUT_Y,      &key_map},
    {PAPP_INPUT_START,  &key_use},
    {PAPP_INPUT_SELECT, &key_weapontoggle},
};

#define NUM_KEYS (sizeof(keymap) / sizeof(keymap[0]))

/* ── I_StartTic — Input polling ──────────────────────────────────────── */

void I_StartTic(void)
{
    static int prev_vals[PAPP_INPUT_MAX] = {0};
    papp_gamepad_state_t state;
    event_t event = {0};

    _papp_svc->input_gamepad_read(&state);

    /* Check for exit request (watchdog already handles long press) */
    if (papp_exit_requested) {
        app_return_to_launcher();
    }

    /* Generate key events for changed buttons */
    for (int i = 0; i < (int)NUM_KEYS; i++) {
        int id = keymap[i].papp_id;
        int cur = state.values[id];
        if (cur != prev_vals[id]) {
            event.type = cur ? ev_keydown : ev_keyup;
            event.data1 = *keymap[i].key;
            D_PostEvent(&event);
        }
    }

    /* Update prev state for all buttons (not just mapped ones) */
    for (int i = 0; i < PAPP_INPUT_MAX; i++)
        prev_vals[i] = state.values[i];
}

/* ── Timing ──────────────────────────────────────────────────────────── */

int I_GetTimeMS(void)
{
    return (int)(_papp_svc->get_time_us() / 1000);
}

int I_GetTime(void)
{
    return I_GetTimeMS() * TICRATE * realtic_clock_rate / 100000;
}

void I_uSleep(unsigned long usecs)
{
    /* Convert to ms for the service call, minimum 1ms */
    int ms = (int)(usecs / 1000);
    if (ms < 1) ms = 1;
    _papp_svc->delay_ms(ms);
}

/* ── System Init / Exit ──────────────────────────────────────────────── */

void I_Init(void)
{
    extern int snd_channels;
    extern int snd_samplerate;

    snd_channels = 8;
    snd_samplerate = 22050;
    snd_MusicVolume = 15;
    snd_SfxVolume = 15;
    realtic_clock_rate = 100;  /* normal speed */
}

void I_SafeExit(int rc)
{
    app_return_to_launcher();
    /* longjmp in app_return_to_launcher means we never reach here */
    for (;;) {}
}

const char *I_DoomExeDir(void)
{
    return "/sd/roms/doom";
}

const char *I_SigString(char *buf, size_t sz, int signum)
{
    if (buf && sz > 0) buf[0] = '\0';
    return buf;
}
