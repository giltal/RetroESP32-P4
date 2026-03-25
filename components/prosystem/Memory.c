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
// Memory.cpp
// ----------------------------------------------------------------------------
#include "Memory.h"
#include "Equates.h"
#include "Bios.h"
#include "Cartridge.h"
#include "Tia.h"
#include "Riot.h"

#include <string.h>

#include <esp_heap_caps.h>
#include <esp_attr.h>

uint8_t *memory_ram = NULL;
//uint8_t* memory_rom; //[MEMORY_SIZE] = {0};
uint8_t* fastmap[16];
uint8_t* cartRAM;

#define MEM_READ(address) (fastmap[(address) >> 12][(address) & 0xfff])
#define MEM_WRITE(address, data) { fastmap[(address) >> 12][(address) & 0xfff] = (data); }
// ----------------------------------------------------------------------------
// Reset
// ----------------------------------------------------------------------------
void memory_Reset(void)
{
    // if (!memory_rom)
    // {
    //     //memory_ram = heap_caps_malloc(MEMORY_SIZE, MALLOC_CAP_SPIRAM);
    //     // memory_ram = malloc(MEMORY_SIZE);
    //     // if (!memory_ram) abort();
    //
    //     memory_rom = heap_caps_malloc(MEMORY_SIZE, MALLOC_CAP_SPIRAM);
    //     if (!memory_rom) abort();
    // }

    if (!memory_ram)
    {
        memory_ram = heap_caps_malloc(0x4000, MALLOC_CAP_SPIRAM);
        if (!memory_ram) abort();
    }

    if (!cartRAM)
    {
        cartRAM = heap_caps_malloc(16384, MALLOC_CAP_SPIRAM);
        if (!cartRAM) abort();
    }

   uint32_t index;
   for(index = 0; index < 0x4000; index++)
   {
      memory_ram[index] = 0;
      //memory_rom[index] = 1;
   }
   //for(index = 0; index < 16384; index++)
      //memory_rom[index] = 0;

    for (int i = 0; i < 4; ++i)
    {
        fastmap[i] = &memory_ram[i * 0x1000];
    }
}
// ----------------------------------------------------------------------------
// Read
// ----------------------------------------------------------------------------
IRAM_ATTR uint8_t memory_Read(uint16_t address)
{
   switch ( address )
   {
      case INTIM:
      case INTIM | 0x2:
         memory_ram[INTFLG] &= 0x7f;
         return memory_ram[INTIM];
      case INTFLG:
      case INTFLG | 0x2:
         memory_ram[INTFLG] &= 0x7f;
         return memory_ram[INTFLG];
      default:
         return MEM_READ(address);
   }

   //return memory_ram[address];

}

// ----------------------------------------------------------------------------
// Write
// ----------------------------------------------------------------------------
IRAM_ATTR void memory_Write(uint16_t address, uint8_t data)
{
   if(address < 0x4000)
   {
      switch(address)
      {
         case WSYNC:
            if(!(cartridge_flags & 128))
               memory_ram[WSYNC] = true;
            break;
         case INPTCTRL:
            if(data == 22 && cartridge_IsLoaded( ))
               cartridge_Store( );
            else if(data == 2 && bios_enabled)
               bios_Store( );
            break;
         case INPT0:
         case INPT1:
         case INPT2:
         case INPT3:
         case INPT4:
         case INPT5:
            break;
         case AUDC0:
            tia_SetRegister(AUDC0, data);
            break;
         case AUDC1:
            tia_SetRegister(AUDC1, data);
            break;
         case AUDF0:
            tia_SetRegister(AUDF0, data);
            break;
         case AUDF1:
            tia_SetRegister(AUDF1, data);
            break;
         case AUDV0:
            tia_SetRegister(AUDV0, data);
            break;
         case AUDV1:
            tia_SetRegister(AUDV1, data);
            break;
         case SWCHA:	/*gdement:  Writing here actually writes to DRA inside the RIOT chip.
                       This value only indirectly affects output of SWCHA.  Ditto for SWCHB.*/
            riot_SetDRA(data);
            break;
         case SWCHB:
            riot_SetDRB(data);
            break;
         case TIM1T:
         case TIM1T | 0x8:
            riot_SetTimer(TIM1T, data);
            break;
         case TIM8T:
         case TIM8T | 0x8:
            riot_SetTimer(TIM8T, data);
            break;
         case TIM64T:
         case TIM64T | 0x8:
            riot_SetTimer(TIM64T, data);
            break;
         case T1024T:
         case T1024T | 0x8:
            riot_SetTimer(T1024T, data);
            break;
         default:
            MEM_WRITE(address, data);
            if(address >= 8256 && address <= 8447)
            {
               MEM_WRITE(address - 8192, data);
           }
            else if(address >= 8512 && address <= 8702)
            {
               MEM_WRITE(address - 8192, data);
           }
            else if(address >= 64 && address <= 255)
            {
               MEM_WRITE(address + 8192, data);
           }
            else if(address >= 320 && address <= 511)
            {
               MEM_WRITE(address + 8192, data);
           }
            break;
            /*TODO: gdement:  test here for debug port.  Don't put it in the switch because that will change behavior.*/
      }
   }
   else
      cartridge_Write(address, data);
}

// ----------------------------------------------------------------------------
// WriteROM
// ----------------------------------------------------------------------------
void memory_WriteROM(uint16_t address, uint16_t size, const uint8_t* data)
{
   //uint32_t index;

   //if((address + size) <= MEMORY_SIZE && data != NULL)
   {
    //   for(index = 0; index < size; index++)
    //   {
    //      memory_ram[address + index] = data[index];
    //      //memory_rom[address + index] = 1;
    //   }
        //memcpy(&memory_ram[address], data, size);
   }
   // printf("%s: address=%#06x, size=%#06x, data=%p\n",
   //      __func__, address, size, data);

   for (int i = 0; i < size; i += 0x1000)
   {
    //    printf("%s: i=%d, address=%#06x, data=%p\n",
    //         __func__, i, address + i, data + i);
       fastmap[(address + i) >> 12] = (uint8_t*)(data + i);
   }
}

// ----------------------------------------------------------------------------
// ClearROM
// ----------------------------------------------------------------------------
void memory_ClearROM(uint16_t address, uint16_t size)
{
   //uint32_t index;

   //if((address + size) <= MEMORY_SIZE)
   {
    //   for(index = 0; index < size; index++)
    //   {
    //      memory_ram[address + index] = 0;
    //      //memory_rom[address + index] = 0;
    //   }
        //memset(&memory_ram[address], 0, size);
   }

   //cartRAM
   // printf("%s: address=%#06x, size=%#06x\n",
   //      __func__, address, size);

   if (size > 16384) abort();

   for (int i = 0; i < size; i += 0x1000)
   {
       fastmap[(address + i) >> 12] = (uint8_t*)(cartRAM + i);
   }
}
