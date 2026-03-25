/*
 * Quake Input shim for PSRAM App — replaces in_esp32.c.
 *
 * Routes gamepad input through app_services_t::input_gamepad_read()
 * instead of rg_input_read_gamepad().
 */
#include "psram_app.h"
#include <string.h>
#include <stdint.h>

#include "quakedef.h"

extern const app_services_t *_papp_svc;
extern volatile int papp_exit_requested;
extern void app_return_to_launcher(void);

static int prev_vals[PAPP_INPUT_MAX] = {0};

void IN_Init(void)
{
}

void IN_Shutdown(void)
{
}

void IN_Commands(void)
{
    papp_gamepad_state_t state;
    _papp_svc->input_gamepad_read(&state);

    /* Check for exit request */
    if (papp_exit_requested) {
        Sys_Quit();
        return;
    }

    /* Directionals */
    if (state.values[PAPP_INPUT_UP] != prev_vals[PAPP_INPUT_UP])
        Key_Event(K_UPARROW, state.values[PAPP_INPUT_UP] != 0);
    if (state.values[PAPP_INPUT_DOWN] != prev_vals[PAPP_INPUT_DOWN])
        Key_Event(K_DOWNARROW, state.values[PAPP_INPUT_DOWN] != 0);
    if (state.values[PAPP_INPUT_LEFT] != prev_vals[PAPP_INPUT_LEFT])
        Key_Event(K_LEFTARROW, state.values[PAPP_INPUT_LEFT] != 0);
    if (state.values[PAPP_INPUT_RIGHT] != prev_vals[PAPP_INPUT_RIGHT])
        Key_Event(K_RIGHTARROW, state.values[PAPP_INPUT_RIGHT] != 0);

    /* Start / Select */
    if (state.values[PAPP_INPUT_START] != prev_vals[PAPP_INPUT_START])
        Key_Event(K_ESCAPE, state.values[PAPP_INPUT_START] != 0);
    if (state.values[PAPP_INPUT_SELECT] != prev_vals[PAPP_INPUT_SELECT]) {
        if (state.values[PAPP_INPUT_SELECT])
            Cbuf_AddText("impulse 10\n"); /* Weapon toggle */
    }

    /* A Button: Fire in game, Enter in menu */
    if (state.values[PAPP_INPUT_A] != prev_vals[PAPP_INPUT_A]) {
        int down = state.values[PAPP_INPUT_A] != 0;
        if (key_dest == key_game) {
            Key_Event(K_CTRL, down);
        } else {
            Key_Event(K_ENTER, down);
            if (down) {
                Key_Event('y', 1);
                Key_Event('y', 0);
            }
        }
    }

    /* B Button: Jump in game, Escape in menu */
    if (state.values[PAPP_INPUT_B] != prev_vals[PAPP_INPUT_B]) {
        int down = state.values[PAPP_INPUT_B] != 0;
        if (key_dest == key_game) {
            Key_Event(K_SPACE, down);
        } else {
            Key_Event(K_ESCAPE, down);
            if (down) {
                Key_Event('n', 1);
                Key_Event('n', 0);
            }
        }
    }

    /* X: Swim down / movedown */
    if (state.values[PAPP_INPUT_X] != prev_vals[PAPP_INPUT_X])
        Key_Event('c', state.values[PAPP_INPUT_X] != 0);

    /* Y: Run/Walk toggle */
    if (state.values[PAPP_INPUT_Y] != prev_vals[PAPP_INPUT_Y])
        Key_Event(K_SHIFT, state.values[PAPP_INPUT_Y] != 0);

    /* Update previous state */
    for (int i = 0; i < PAPP_INPUT_MAX; i++)
        prev_vals[i] = state.values[i];
}

void IN_Move(usercmd_t *cmd)
{
    papp_gamepad_state_t state;
    _papp_svc->input_gamepad_read(&state);

    if (state.values[PAPP_INPUT_UP])    cmd->forwardmove += 200;
    if (state.values[PAPP_INPUT_DOWN])  cmd->forwardmove -= 200;
    if (state.values[PAPP_INPUT_LEFT])  cmd->viewangles[YAW] += 1000;
    if (state.values[PAPP_INPUT_RIGHT]) cmd->viewangles[YAW] -= 1000;

    /* Strafe on shoulder buttons */
    if (state.values[PAPP_INPUT_L]) cmd->sidemove -= 200;
    if (state.values[PAPP_INPUT_R]) cmd->sidemove += 200;
}
