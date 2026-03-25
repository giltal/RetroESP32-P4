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
#include "config.h"
#include "config_file.h"
#include "file.h"
#include "joystick.h"
#include "keyboard.h"
#include "nortsong.h"
#include "opentyr.h"
#include "params.h"
#include "varz.h"
#include "video.h"

#include <assert.h>
#include <ctype.h>
#include <string.h>

int joystick_axis_threshold( int j, int value );
int check_assigned( SDL_Joystick *joystick_handle, const Joystick_assignment assignment[2] );

const char *assignment_to_code( const Joystick_assignment *assignment );
void code_to_assignment( Joystick_assignment *assignment, const char *buffer );

int joystick_repeat_delay = 300; // milliseconds, repeat delay for buttons
bool joydown = false;            // any joystick buttons down, updated by poll_joysticks()
bool ignore_joystick = false;

int joysticks = 0;
Joystick *joystick = NULL;

static const char joystick_cfg_version = 1;
static const int joystick_analog_max = 32767;

// eliminates axis movement below the threshold
int joystick_axis_threshold( int j, int value )
{
	return 0;
}

// converts joystick axis to sane Tyrian-usable value (based on sensitivity)
int joystick_axis_reduce( int j, int value )
{
	return 0;
}

// converts analog joystick axes to an angle
// returns false if axes are centered (there is no angle)
bool joystick_analog_angle( int j, float *angle )
{
	return false;
}

/* gives back value 0..joystick_analog_max indicating that one of the assigned
 * buttons has been pressed or that one of the assigned axes/hats has been moved
 * in the assigned direction
 */
int check_assigned( SDL_Joystick *joystick_handle, const Joystick_assignment assignment[2] )
{
	return 0;
}

// updates joystick state
void poll_joystick( int j )
{
}

// updates all joystick states
void poll_joysticks( void )
{
}

// sends SDL KEYDOWN and KEYUP events for a key
void push_key( SDLKey key )
{
}

// helps us be lazy by pretending joysticks are a keyboard (useful for menus)
void push_joysticks_as_keyboard( void )
{
}

// initializes SDL joystick system and loads assignments for joysticks found
void init_joysticks( void )
{
}

// deinitializes SDL joystick system and saves joystick assignments
void deinit_joysticks( void )
{
}

void reset_joystick_assignments( int j )
{
}

static const char* const assignment_names[] =
{
	"up",
	"right",
	"down",
	"left",
	"fire",
	"change fire",
	"left sidekick",
	"right sidekick",
	"menu",
	"pause",
};

bool load_joystick_assignments( Config *config, int j )
{
	return true;
}

bool save_joystick_assignments( Config *config, int j )
{
	return true;
}

// fills buffer with comma separated list of assigned joystick functions
void joystick_assignments_to_string( char *buffer, size_t buffer_len, const Joystick_assignment *assignments )
{
}

// reverse of assignment_to_code()
void code_to_assignment( Joystick_assignment *assignment, const char *buffer )
{
}

/* gives the short (6 or less characters) identifier for a joystick assignment
 * 
 * two of these per direction/action is all that can fit on the joystick config screen,
 * assuming two digits for the axis/button/hat number
 */
const char *assignment_to_code( const Joystick_assignment *assignment )
{
	return "";
}

// captures joystick input for configuring assignments
// returns false if non-joystick input was detected
// TODO: input from joystick other than the one being configured probably should not be ignored
bool detect_joystick_assignment( int j, Joystick_assignment *assignment )
{
	return false;
}

// compares relevant parts of joystick assignments for equality
bool joystick_assignment_cmp( const Joystick_assignment *a, const Joystick_assignment *b )
{
	return false;
}

