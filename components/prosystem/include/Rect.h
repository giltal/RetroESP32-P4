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
// Rect.h
// ----------------------------------------------------------------------------
#ifndef RECT_H
#define RECT_H

#include <stdint.h>

#include "defs.h"


#ifdef __cplusplus
extern "C" {
#endif

struct Rects
{
   uint32_t left;
   uint32_t top;
   uint32_t right;
   uint32_t bottom;
};

static INLINE uint32_t Rect_GetLength(struct Rects *rect)
{
   return (rect->right - rect->left) + 1;
}

static INLINE uint32_t Rect_GetHeight(struct Rects *rect)
{
   return (rect->bottom - rect->top) + 1;
}

static INLINE uint32_t Rect_GetArea(struct Rects *rect)
{
   return Rect_GetLength(rect) * Rect_GetHeight(rect);
}

typedef struct Rects rect;

#ifdef __cplusplus
}
#endif

#endif
