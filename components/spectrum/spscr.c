/* 
 * Copyright (C) 1996-1998 Szeredi Miklos
 * Email: mszeredi@inf.bme.hu
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version. See the file COPYING. 
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#include "spscr_p.h"
#include "spscr.h"

#include "spperif.h"
#include "z80.h"


#include <stdlib.h>
#include <stdio.h>
#include <string.h>

int color_type = 0;

#define N0 0x04
#define N1 0x34

#define B0 0x08
#define B1 0x3F

struct rgb *spscr_crgb;

static struct rgb norm_colors[COLORNUM]={
  {0,0,0},{N0,N0,N1},{N1,N0,N0},{N1,N0,N1},
  {N0,N1,N0},{N0,N1,N1},{N1,N1,N0},{N1,N1,N1},

  {0x15,0x15,0x15},{B0,B0,B1},{B1,B0,B0},{B1,B0,B1},
  {B0,B1,B0},{B0,B1,B1},{B1,B1,B0},{B1,B1,B1}
};

static struct rgb gray_colors[COLORNUM]={
  {0,0,0},{20,20,20},{26,26,26},{32,32,32},
  {38,38,38},{44,44,44},{50,50,50},{56,56,56},

  {16,16,16},{23,23,23},{30,30,30},{36,36,36},
  {43,43,43},{50,50,50},{56,56,56},{63,63,63}
};

struct rgb custom_colors[COLORNUM]={
  {0,0,0},{N0,N0,N1},{N1,N0,N0},{N1,N0,N1},
  {N0,N1,N0},{N0,N1,N1},{N1,N1,N0},{N1,N1,N1},

  {0x15,0x15,0x15},{B0,B0,B1},{B1,B0,B0},{B1,B0,B1},
  {B0,B1,B0},{B0,B1,B1},{B1,B1,B0},{B1,B1,B1}
};


#define TABOFFS 2

volatile int screen_visible = 1;
volatile int accept_keys = 1;

int my_lastborder=100,skip=0;

extern int buffpos;  // audio buffer position
extern int keyboard; // on-screen keyboard active?

/* Shared framebuffer: 320×240 RGB565, allocated externally (PSRAM) */
extern uint16_t *sp_framebuffer;

//-------------------------------------------------------------------------------------------
byte *update_screen_line(byte *scrp, int coli, int scri, int border,
			 qbyte *cmarkp)
{
  int addr,attr,i,y,paper=0,ink=0,tmp;
  byte a=0;
  qbyte *scrptr;
  // 8 Spectrum colours as 565...  (565 is BRG, but spectrum is GRB...)
  static uint16_t colours[16]={
      0b0000000000000000, // ___ black
      0b1111000000000000, // B__ blue
      0b0000011111000000, // _R__ red
      0b1111011111000000, // BR_ magenta
      0b0000000000011110, // __G green
      0b1111000000011110, // B_G cyan
      0b0000011111011110, // _RG_ yellow
      0b1111011111011110,  // RBG white
      // BRIGHT versions...
      0b0000000000000000, // ___ black
      0b1111100000000000, // B__ blue
      0b0000011111100000, // _R__ red
      0b1111111111100000, // BR_ magenta
      0b0000000000011111, // __G green
      0b1111100000011111, // B_G cyan
      0b0000011111111111, // _RG_ yellow
      0b1111111111111111  // RBG white
  };

  if (!sp_framebuffer) return (byte *)scrp;

  // first, see if border colour changed, update only if so (for speed..)
  if (scri>0 && coli==0) { // start of new frame....
     if (border!=my_lastborder && skip==1) { // redraw border into framebuffer
      uint16_t bc = colours[border];
      /* Top border: rows 0..23, full 320 width */
      for (i = 0; i < 24 * 320; i++) sp_framebuffer[i] = bc;
      /* Bottom border: rows 216..239 */
      for (i = 216 * 320; i < 240 * 320; i++) sp_framebuffer[i] = bc;
      /* Left border: col 0..31 in rows 24..215 */
      for (y = 24; y < 216; y++)
        for (i = 0; i < 32; i++) sp_framebuffer[y * 320 + i] = bc;
      /* Right border: col 288..319 in rows 24..215 */
      for (y = 24; y < 216; y++)
        for (i = 288; i < 320; i++) sp_framebuffer[y * 320 + i] = bc;

      my_lastborder=border; skip=0;
     }
  }
  if (coli==191) skip=1; // can now re-draw border next frame if required

  scrptr=scrp;

  if (scri>0 && skip) { // normal lines only for now....
    y=coli;
    // screen address for pixels for this line...
    addr=y/64; y-=addr*64; addr*=8;
    addr+=y%8; addr*=8;
    addr+=y/8;
    addr=addr*32+16384+31;
    // screen address for ATTRIBUTE colours for this line...
    attr=coli/8*32+22528+31;

    /* Write directly into framebuffer at row (coli+24), starting at col 32 */
    uint16_t *row = &sp_framebuffer[(coli + 24) * 320 + 32];
    for (i=255; i>=0; i--) {
      if (i%8==7) { // update colourmap and pixels-byte
         a=PRNM(proc).mem[attr]; attr--;
	 if (a&64) paper=colours[a%8+8]; else paper=colours[a%8];
	 if (a&64) ink=colours[(a/8)%8+8]; else ink=colours[(a/8)%8];
	 if (a&128 && sp_flash_state) {tmp=ink; ink=paper; paper=tmp;}
         a=PRNM(proc).mem[addr]; addr--;
      }
      // update pixel directly in framebuffer...
      row[i] = (a%2==1) ? paper : ink;
      a/=2;
    }
  }
  return (byte *) scrptr;
}
//-----------------------------------------------------------------------------------------------------


void translate_screen(void)
{
  int border, scline;
  byte *scrptr;
  qbyte cmark = 0;

  scrptr = (byte *) SPNM(image);

  border = DANM(ula_outport) & 0x07;
  if(border != SPNM(lastborder)) {
    SPNM(border_update) = 2;
    SPNM(lastborder) = border;
  }

  for(scline = 0; scline < (TMNUM / 2); scline++) 
    scrptr = update_screen_line(scrptr, SPNM(coli)[scline], SPNM(scri)[scline],
				border, &cmark);

}

void flash_change(void)
{
  int i,j;
  byte *scp;
  qbyte *mcp;
  register unsigned int val;

  mcp = sp_scr_mark + 0x2C0;
  scp = z80_proc.mem + 0x5800;

  for(i = 24; i; mcp++, i--) {
    val = 0;
    for(j = 32; j; scp++, j--) {
      val >>= 1;
      if(*scp & 0x80) val |= (1 << 31);
    }
    *mcp |= val;
  }
}

void spscr_init_line_pointers(int lines)
{
  int i;
  int bs;
  int y;
  int scline;
  
  bs = (lines - 192) / 2;

  for(i = 0; i < PORT_TIME_NUM; i++) {

    sp_scri[i] = -2;

    if(i < ODDHF) scline = i;
    else scline = i - ODDHF;
    
    if(scline >= 64-bs && scline < 256+bs) {
      if(scline >= 64 && scline < 256) {
	y = scline - 64;
	sp_coli[i] = y;
	sp_scri[i] = 0x200 +
	  (y & 0xC0) + ((y & 0x07) << 3) + ((y & 0x38) >> 3);
      }
      else sp_scri[i] = -1;
    }
  }
}

void spscr_init_colors(void)
{
  spscr_crgb = norm_colors;
  
  switch(color_type) {
  case 0:
    spscr_crgb = norm_colors;
    break;
    
  case 1:
    spscr_crgb = gray_colors;
    break;
    
  case 2:
    spscr_crgb = custom_colors;
    break;
  }
}
