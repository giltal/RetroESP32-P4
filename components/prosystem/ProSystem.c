// ----------------------------------------------------------------------------
//   ___  ___  ___  ___       ___  ____  ___  _  _
//  /__/ /__/ /  / /__  /__/ /__    /   /_   / |/ /
// /    / \  /__/ ___/ ___/ ___/   /   /__  /    /  emulator
//
// ----------------------------------------------------------------------------
// Copyright 2003, 2004 Greg Stanton
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
// ProSystem.cpp
// ----------------------------------------------------------------------------
#include "ProSystem.h"
#include <string.h>
#include "Equates.h"
#include "Bios.h"
#include "Cartridge.h"
#include "Maria.h"
#include "Memory.h"
#include "Region.h"
#include "Riot.h"
#include "Sally.h"
#include "Tia.h"
#include "Pokey.h"

#include <stdlib.h>

#include <esp_attr.h>

#define PRO_SYSTEM_STATE_HEADER "PRO-SYSTEM STATE"
#define PRO_SYSTEM_SOURCE "ProSystem.cpp"

uint16_t prosystem_frequency = 60;
uint8_t prosystem_frame = 0;
uint16_t prosystem_scanlines = 262;
uint32_t prosystem_cycles = 0;

extern uint8_t *memory_ram;

// ----------------------------------------------------------------------------
// Reset
// ----------------------------------------------------------------------------
void prosystem_Reset(void)
{
   if(!cartridge_IsLoaded())
      return;

   prosystem_frame = 0;
   sally_Reset();
   region_Reset( );
   tia_Clear( );
   tia_Reset( );
   pokey_Clear( );
   pokey_Reset( );
   memory_Reset( );
   maria_Clear( );
   maria_Reset( );
   riot_Reset ( );

   if(bios_enabled)
      bios_Store( );
   else
      cartridge_Store( );

   prosystem_cycles = sally_ExecuteRES( );
}

// ----------------------------------------------------------------------------
// ExecuteFrame
// ----------------------------------------------------------------------------
IRAM_ATTR void prosystem_ExecuteFrame(const uint8_t* input)
{
   riot_SetInput(input);

   for(maria_scanline = 1; maria_scanline <= prosystem_scanlines; maria_scanline++)
   {
      uint32_t cycles;
      if(maria_scanline == maria_displayArea.top)
         memory_ram[MSTAT] = 0;
      if(maria_scanline == maria_displayArea.bottom)
         memory_ram[MSTAT] = 128;

      prosystem_cycles %= 456;
      while(prosystem_cycles < 28)
      {
         cycles = sally_ExecuteInstruction();
         prosystem_cycles += (cycles << 2);

         if(riot_timing)
            riot_UpdateTimer(cycles);

         if(memory_ram[WSYNC] && !(cartridge_flags & CARTRIDGE_WSYNC_MASK))
         {
            prosystem_cycles = 456;
            memory_ram[WSYNC] = false;
            break;
         }
      }

      cycles = maria_RenderScanline( );
      if(cartridge_flags & CARTRIDGE_CYCLE_STEALING_MASK)
         prosystem_cycles += cycles;

      while(prosystem_cycles < 456)
      {
         cycles = sally_ExecuteInstruction( );
         prosystem_cycles += (cycles << 2);
         if(riot_timing)
            riot_UpdateTimer(cycles);

         if(memory_ram[WSYNC] && !(cartridge_flags & CARTRIDGE_WSYNC_MASK))
         {
            prosystem_cycles = 456;
            memory_ram[WSYNC] = false;
            break;
         }
      }
      tia_Process(2);
      if(cartridge_pokey)
         pokey_Process(2);
   }

   prosystem_frame++;

   if(prosystem_frame >= prosystem_frequency)
      prosystem_frame = 0;
}

// ----------------------------------------------------------------------------
// Save
// ----------------------------------------------------------------------------
bool prosystem_Save(char *buffer, bool compress)
{
   uint32_t size = 0;
   uint32_t index;

   for(index = 0; index < 16; index++)
      buffer[size + index] = PRO_SYSTEM_STATE_HEADER[index];
   size += 16;

   buffer[size++] = 1;
   for(index = 0; index < 4; index++)
      buffer[size + index] = 0;
   size += 4;

   for(index = 0; index < 32; index++)
      buffer[size + index] = cartridge_digest[index];
   size += 32;

   buffer[size++] = sally_a;
   buffer[size++] = sally_x;
   buffer[size++] = sally_y;
   buffer[size++] = sally_p;
   buffer[size++] = sally_s;
   buffer[size++] = sally_pc.b.l;
   buffer[size++] = sally_pc.b.h;
   buffer[size++] = cartridge_bank;

   for(index = 0; index < 16384; index++)
      buffer[size + index] = memory_ram[index];
   size += 16384;

   if(cartridge_type == CARTRIDGE_TYPE_SUPERCART_RAM)
   {
       abort();
    //   for(index = 0; index < 16384; index++)
    //      buffer[size + index] = memory_ram[16384 + index];
    //   size += 16384;
   }

   return true;
}

// ----------------------------------------------------------------------------
// Load
// ----------------------------------------------------------------------------
bool prosystem_Load(const char *buffer)
{
   uint32_t index;
   uint8_t version;
   char digest[33] = {0};
   uint32_t date   = 0;
   uint32_t size   = 0;
   uint32_t offset = 0;

   for(index = 0; index < 16; index++)
   {
      /* File is not a valid ProSystem save state. */
      if(buffer[offset + index] != PRO_SYSTEM_STATE_HEADER[index])
         return false;
   }
   offset += 16;
   version = buffer[offset++];

   for(index = 0; index < 4; index++);
   offset += 4;

   for(index = 0; index < 32; index++)
      digest[index] = buffer[offset + index];

   offset += 32;

   /* Does not match loaded cartridge digest? */
   if(strcmp(cartridge_digest, digest) != 0)
      return false;

   sally_a = buffer[offset++];
   sally_x = buffer[offset++];
   sally_y = buffer[offset++];
   sally_p = buffer[offset++];
   sally_s = buffer[offset++];
   sally_pc.b.l = buffer[offset++];
   sally_pc.b.h = buffer[offset++];

   cartridge_StoreBank(buffer[offset++]);

   for(index = 0; index < 16384; index++)
      memory_ram[index] = buffer[offset + index];
   offset += 16384;

   if(cartridge_type == CARTRIDGE_TYPE_SUPERCART_RAM)
   {
      /* Save state file has an invalid size. */
      if(size != 32829)
         return false;

      for(index = 0; index < 16384; index++)
         memory_ram[16384 + index] = buffer[offset + index];
      offset += 16384;
   }

   return true;
}

// ----------------------------------------------------------------------------
// Close
// ----------------------------------------------------------------------------
void prosystem_Close(void)
{
   cartridge_Release( );
   maria_Reset( );
   maria_Clear( );
   memory_Reset( );
   tia_Reset( );
   tia_Clear( );
}
