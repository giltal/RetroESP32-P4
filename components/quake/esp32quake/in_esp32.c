#include "quakedef.h"
#include "rg_system.h"

static uint32_t prev_gamepad = 0;

void IN_Init(void)
{
}

void IN_Shutdown(void)
{
}

void IN_Commands(void)
{
    uint32_t gamepad = rg_input_read_gamepad();
    uint32_t changed = gamepad ^ prev_gamepad;

    if (changed)
    {
        // Simple directionals
        if (changed & RG_KEY_UP)    Key_Event(K_UPARROW, (gamepad & RG_KEY_UP) != 0);
        if (changed & RG_KEY_DOWN)  Key_Event(K_DOWNARROW, (gamepad & RG_KEY_DOWN) != 0);
        if (changed & RG_KEY_LEFT)  Key_Event(K_LEFTARROW, (gamepad & RG_KEY_LEFT) != 0);
        if (changed & RG_KEY_RIGHT) Key_Event(K_RIGHTARROW, (gamepad & RG_KEY_RIGHT) != 0);

        // System/Menu access
        if (changed & RG_KEY_MENU) {
            Key_Event(K_ESCAPE, (gamepad & RG_KEY_MENU) != 0);
        }

        // Start / Select
        if (changed & RG_KEY_START)  Key_Event(K_ESCAPE, (gamepad & RG_KEY_START) != 0);
        if (changed & RG_KEY_SELECT) {
            if ((gamepad & RG_KEY_SELECT)) {
                Cbuf_AddText("impulse 10\n"); // Weapon toggle (matches Doom)
            }
        }

        // A Button: Fire in game, Enter in menu, 'y' in confirmation
        if (changed & RG_KEY_A) {
            bool down = (gamepad & RG_KEY_A) != 0;
            if (key_dest == key_game) {
                Key_Event(K_CTRL, down);
            } else {
                Key_Event(K_ENTER, down);
                if (down) { // Send 'y' for confirmations
                    Key_Event('y', true);
                    Key_Event('y', false);
                }
            }
        }

        // B Button: Jump in game, Escape in menu, 'n' in confirmation
        if (changed & RG_KEY_B) {
            bool down = (gamepad & RG_KEY_B) != 0;
            if (key_dest == key_game) {
                Key_Event(K_SPACE, down);
            } else {
                Key_Event(K_ESCAPE, down);
                if (down) { // Send 'n' for confirmations
                    Key_Event('n', true);
                    Key_Event('n', false);
                }
            }
        }

        // Additional Game Controls
        if (changed & (RG_KEY_X | RG_KEY_OPTION)) Key_Event('c', (gamepad & (RG_KEY_X | RG_KEY_OPTION)) != 0);      // Swim down (bound to +movedown)
        if (changed & RG_KEY_Y) Key_Event(K_SHIFT, (gamepad & RG_KEY_Y) != 0); // Run / Walk
    }
    prev_gamepad = gamepad;
    
    // Fallback Retro-Go menu trigger
    if ((gamepad & RG_KEY_START) && (gamepad & RG_KEY_SELECT))
    {
        rg_gui_game_menu();
    }
}

void IN_Move(usercmd_t *cmd)
{
    uint32_t gamepad = rg_input_read_gamepad();
    
    if (gamepad & RG_KEY_UP) cmd->forwardmove += 200;
    if (gamepad & RG_KEY_DOWN) cmd->forwardmove -= 200;
    if (gamepad & RG_KEY_LEFT) cmd->viewangles[YAW] += 1000;
    if (gamepad & RG_KEY_RIGHT) cmd->viewangles[YAW] -= 1000;
    
    // Strafe on Shoulder buttons
    if (gamepad & RG_KEY_L) cmd->sidemove -= 200;
    if (gamepad & RG_KEY_R) cmd->sidemove += 200;
}
