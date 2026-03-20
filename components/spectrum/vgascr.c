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

#include "config.h"

#include "vgascr.h"
#include "spperif.h"
#include "spscr.h"
#include "spscr_p.h"
#include "spkey_p.h"
#include "misc.h"
#include "interf.h"

extern void vga_drawscanline();
extern void vga_setmode();
extern void vga_setpalvec();

#define TV_WIDTH         320
#define TV_HEIGHT_SMALL  200
#define TV_HEIGHT_LARGE  240

#define X_OFF        ((TV_WIDTH - WIDTH) / 2)
#define Y_OFF_L      ((TV_HEIGHT_LARGE - HEIGHT) / 2)


#define WIDTH      256
#define HEIGHT     192

int small_screen = 1;
int vga_pause_bg = 0;

int scrmul;           /* DUMMY */
int use_shm;          /* DUMMY */
int privatemap;       /* DUMMY */
int pause_on_iconify; /* DUMMY */

#ifndef USE_DJGPP

//#include <vga.h>
#include <stdio.h>
#include <stdlib.h>

static volatile int need_restore = 0;

void update_screen(void)
{
  if(need_restore) {
    need_restore = 0;
    spscr_init_line_pointers(small_screen ? TV_HEIGHT_SMALL : TV_HEIGHT_LARGE);
    sp_init_screen_mark();
  }
  if(!small_screen) {
    int i;
    /* TODO: Only update if necessary */

    for(i = 0; i < TV_HEIGHT_LARGE; i++) {
      if(sp_border_update || 
         (i >= Y_OFF_L && i < TV_HEIGHT_LARGE - Y_OFF_L && 
          sp_imag_mark[i - Y_OFF_L])) {
	
        if(i >= Y_OFF_L && i < TV_HEIGHT_LARGE - Y_OFF_L) 
          sp_imag_mark[i - Y_OFF_L] = 0;

	vga_drawscanline(i, &sp_image[i * TV_WIDTH]);
      }

    }
  }
}

#if RUN_IN_BACKGROUND && defined(VGA_GOTOBACK)
static void noupdt(void)
{
  int i;

  for(i = 0; i < PORT_TIME_NUM; i++) sp_scri[i] = -2;
  screen_visible = 0;
  accept_keys = 0;
}

static int paused_bak = 0;

static void swto(void)
{
  if(vga_pause_bg) paused_bak = sp_paused, sp_paused = 1;
  noupdt();
}

static void swfrom(void)
{
  if(vga_pause_bg && sp_paused) sp_paused = paused_bak;
  
  need_restore = 1;
  screen_visible = 1;
  accept_keys = 1;

  spvk_after_switch = 1;
}

static void start_background(void)
{
  if(vga_runinbackground_version() == 1) {
    vga_runinbackground(VGA_GOTOBACK, swto);
    vga_runinbackground(VGA_COMEFROMBACK, swfrom);
    vga_runinbackground(1);
  }
}

#else /* RUN_IN_BACKGROUND */

static void start_background(void)
{
}
#endif /* RUN_IN_BACKGROUND */

void set_vga_spmode(void)
{
  vga_setmode(); // bjs 320x200x256

  vga_setpalvec(1, COLORNUM, &spscr_crgb[0].r);
}

void restore_sptextmode(void)
{
 // vga_setmode(TEXT);
}


void init_spect_scr(void)
{
  int i;

  
  spscr_init_colors();
  start_background();
  set_vga_spmode();
  if(small_screen) sp_image = NULL;
  else sp_image = (char *) malloc_err(TV_WIDTH * TV_HEIGHT_LARGE);

  for(i = 0; i < COLORNUM; i++) {
    sp_colors[i] = i+1;
  } 
  spscr_init_line_pointers(small_screen ? TV_HEIGHT_SMALL : TV_HEIGHT_LARGE);
}

void sp_init_vga(void)
{
  //vga_init();
  printf("\n");
}

#else /* !USE_DJGPP */

#ifdef Z80C
#warning Compile with i386 assembly!
#endif


#include <stdio.h>
#include <stdlib.h>
#include <dpmi.h>
#include <go32.h>
#include <sys/farptr.h>
#include <stdlib.h>

#ifdef USE_ALLEGRO
#include "allegro.h"
#endif

static int origmode;
int djsp_video_segment;

#define VIDEO_LEN     64000
#define VIDEO_OFFSET  0x000a0000

static void set_video_mode(int mode)
{
  __dpmi_regs r;

  r.h.ah = 0x00;
  r.h.al = mode;
  __dpmi_int(0x10, &r);
}

static int get_video_mode(void)
{
  __dpmi_regs r;
  
  r.h.ah = 0x0f;
  __dpmi_int(0x10, &r);
  return r.h.al;
}

void update_screen(void)
{
#ifdef Z80C
  movedata(_my_ds(), (int) sp_image, VIDEO_OFFSET, VIDEO_LEN);
#endif
}


static void set_pal(int start, int num, int *pal) 
{
  int i;
  __dpmi_regs r;
  
  for(i = 0; i < num * 3; i++) 
    _farpokeb(_dos_ds, (__tb & 0x000fffff)+i, pal[i]);

  r.x.ax = 0x1012;
  r.x.bx = start;
  r.x.cx = num;
  r.x.dx = __tb & 0x0f;
  r.x.es = (__tb >> 4) & 0xffff;
  __dpmi_int(0x10, &r);
}

void set_vga_spmode(void)
{
  set_video_mode(0x13);
  set_pal(1, COLORNUM,  &spscr_crgb[0].r);
  spif_can_print = 0;
}

void restore_sptextmode(void)
{
  set_video_mode(origmode);
  spif_can_print = 1;
}

void init_spect_scr(void)
{
  int i;
 // bjs NOTE! en earlier copy of this function is used instead! Never gets here!
  spscr_init_colors();

#ifdef Z80C
  sp_image = (char *) malloc_err(VIDEO_LEN);
#else
  djsp_video_segment = _dos_ds;
  sp_image = (char *) VIDEO_OFFSET;
#endif


  origmode = get_video_mode();
  atexit(restore_sptextmode);

  set_vga_spmode();

  for(i = 0; i < COLORNUM; i++) {
    sp_colors[i] = i+1;
  }

  spscr_init_mask_color();
  spscr_init_line_pointers(TV_HEIGHT_SMALL);
}

void sp_init_vga(void)
{
#ifdef USE_ALLEGRO
  if(allegro_init() != 0) {
    fprintf(stderr, "Could not initialize allegro\n");
    exit(1);
  }
#endif
}

#endif /* !USE_DJGPP */

void destroy_spect_scr(void)
{
}

void resize_spect_scr(int s)
{
  s = s;
}

void spcf_read_xresources(void)
{
}

void spscr_refresh_colors(void)
{
}
