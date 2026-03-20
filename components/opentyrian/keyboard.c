/* 
 * OpenTyrian: A modern cross-platform port of Tyrian
 * Copyright (C) 2007-2009  The OpenTyrian Development Team
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */
#include "joystick.h"
#include "keyboard.h"
#include "network.h"
#include "opentyr.h"
#include "video.h"
#include "video_scale.h"

#include "SDL.h"
#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/gpio.h"

JE_boolean ESCPressed;

JE_boolean newkey, newmouse, keydown, mousedown;
SDLKey lastkey_sym;
SDLMod lastkey_mod;
unsigned char lastkey_char;
Uint8 lastmouse_but;
Uint16 lastmouse_x, lastmouse_y;
JE_boolean mouse_pressed[3] = {false, false, false};
Uint16 mouse_x, mouse_y;

Uint8 keysactive[SDLK_LAST];

#ifdef NDEBUG
bool input_grab_enabled = true;
#else
bool input_grab_enabled = false;
#endif


void flush_events_buffer( void )
{
}

void wait_input( JE_boolean keyboard, JE_boolean mouse, JE_boolean joystick )
{
}

void wait_noinput( JE_boolean keyboard, JE_boolean mouse, JE_boolean joystick )
{
}

void init_keyboard( void )
{
	newkey = newmouse = false;
	keydown = mousedown = false;
	inputInit();
}

void input_grab( bool enable )
{

}

JE_word JE_mousePosition( JE_word *mouseX, JE_word *mouseY )
{
	*mouseX = mouse_x;
	*mouseY = mouse_y;
	return mousedown ? lastmouse_but : 0;
}

void set_mouse_position( int x, int y )
{
	if (input_grab_enabled)
	{
		mouse_x = x;
		mouse_y = y;
	}
}

void service_SDL_events( JE_boolean clear_new )
{
	
	if (clear_new)
		newkey = newmouse = false;
	
	SDL_Event event;
	while(SDL_PollEvent(&event))
	{
		keysactive[event.key.keysym.sym] = event.key.state;
		if(event.key.state)
			lastkey_sym = event.key.keysym.sym;
		keydown = event.key.state;
		newkey = event.key.state;
		lastkey_char = event.key.keysym.sym;
	}

	/*
	case SDL_QUIT:
		exit(0);
	*/

}

void JE_clearKeyboard( void )
{
	// /!\ Doesn't seems important. I think. D:
}

