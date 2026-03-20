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
#ifndef OPENTYR_H
#define OPENTYR_H

#include "SDL_stdinc.h"
#include <math.h>
#include <stdbool.h>
#include "esp_attr.h"

#define COUNTOF(x) ((unsigned)(sizeof(x) / sizeof *(x)))  // use only on arrays!
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))

#ifndef M_PI
#define M_PI    3.14159265358979323846  // pi
#endif
#ifndef M_PI_2
#define M_PI_2  1.57079632679489661923  // pi/2
#endif
#ifndef M_PI_4
#define M_PI_4  0.78539816339744830962  // pi/4
#endif

typedef unsigned int uint;
typedef unsigned long ulong;

// Pascal types, yuck.
typedef long JE_longint;
//typedef int JE_integer;
typedef short JE_integer;
//typedef short  JE_shortint;
typedef signed char JE_shortint;
typedef unsigned short JE_word;
typedef unsigned char  JE_byte;
typedef bool   JE_boolean;
typedef char   JE_char;
typedef float  JE_real;

#ifdef TYRIAN2000
#define TYRIAN_VERSION "2000"
#else
#define TYRIAN_VERSION "2.1"
#endif

#define TYRIAN_DIR "data"

extern const char *opentyrian_str, *opentyrian_version;

void opentyrian_menu( void );
int main( int argc, char *argv[] );

#endif /* OPENTYR_H */

