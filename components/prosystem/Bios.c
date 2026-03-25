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
// Bios.cpp
// ----------------------------------------------------------------------------
#include <stdio.h>
#include <stdlib.h>
#include "Bios.h"
#include "Memory.h"
#define BIOS_SOURCE "Bios.cpp"

bool bios_enabled = false;

static uint8_t* bios_data = NULL;
static uint16_t bios_size = 0;

// ----------------------------------------------------------------------------
// Load
// ----------------------------------------------------------------------------
bool bios_Load(const char *filename)
{
   FILE *file;
   if(!filename || filename[0] == '\0')
      return false;

   bios_Release();

   file = fopen(filename, "rb");
   if(file == NULL)
      return false;

   if(fseek(file, 0, SEEK_END))
   {
      fclose(file);
      return false;
   }

   bios_size = ftell(file);
   if(fseek(file, 0, SEEK_SET))
   {
      fclose(file);
      return false;
   }

   bios_data = (uint8_t*)malloc(bios_size * sizeof(uint8_t));
   if(fread(bios_data, 1, bios_size, file) != bios_size && ferror(file))
   {
      fclose(file);
      bios_Release();
      return false;
   }

   fclose(file);

   return true;
}

// ----------------------------------------------------------------------------
// IsLoaded
// ----------------------------------------------------------------------------
bool bios_IsLoaded(void)
{
  return (bios_data != NULL)? true: false;
}

// ----------------------------------------------------------------------------
// Release
// ----------------------------------------------------------------------------
void bios_Release(void)
{
   if (bios_data)
      free(bios_data);
   bios_data = NULL;
   bios_size = 0;
}

// ----------------------------------------------------------------------------
// Store
// ----------------------------------------------------------------------------
void bios_Store(void)
{
   if(bios_data != NULL && bios_enabled)
      memory_WriteROM(65536 - bios_size, bios_size, bios_data);
}
