// ----------------------------------------------------------------------------
//   ___  ___  ___  ___       ___  ____  ___  _  _
//  /__/ /__/ /  / /__  /__/ /__    /   /_   / |/ /
// /    / \  /__/ ___/ ___/ ___/   /   /__  /    /  emulator
//
// ----------------------------------------------------------------------------
// Copyright 2005 Greg Stanton
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
// ----------------------------------------------------------------------------
// Sally.h
// ----------------------------------------------------------------------------
#ifndef SALLY_H
#define SALLY_H

#include <stdint.h>
#include "Pair.h"

#ifdef __cplusplus
extern "C" {
#endif

extern void sally_Reset(void);
extern uint32_t sally_ExecuteInstruction(void);
extern uint32_t sally_ExecuteRES(void);
extern uint32_t sally_ExecuteNMI(void);
extern uint32_t sally_ExecuteIRQ(void);

extern uint8_t sally_a;
extern uint8_t sally_x;
extern uint8_t sally_y;
extern uint8_t sally_p;
extern uint8_t sally_s;
extern pair sally_pc;

#ifdef __cplusplus
}
#endif

#endif
