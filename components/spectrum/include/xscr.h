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

#ifndef XSCR_H
#define XSCR_H

#include "ax.h"

typedef unsigned long pixt;

extern Window xsp_win;
extern Display *xsp_disp;
extern int xsp_scr;
extern Colormap xsp_cmap;
extern int xsp_bpp;
extern Visual *xsp_visual;
extern unsigned xsp_depth;
extern unsigned long xsp_colors[];

extern void init_x_scr(aX_default_resources* defres,
		       int *argc, char *argv[]);
extern int allocate_colors(void);
extern void spxs_init_color(void);

#endif /* XSCR_H */
