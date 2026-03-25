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
#include "interf.h"

#include <stdio.h>
#include <ctype.h>

#define MAXMSGLEN   2048

char filenamebuf[MAXFILENAME];
char msgbuf[MAXMSGLEN];
int spif_can_print = 1;

#undef HAVE_READLINE
#ifdef HAVE_READLINE

#include <readline/readline.h>
#include <stdlib.h>

static char *get_filename_line(void)
{
  static char *buf=NULL;
  static char  empty='\0';

  free(buf);
  return ((buf=readline(&empty)) ? buf : &empty);
}

#else /* HAVE_READLINE */

static char *get_filename_line(void)
{
  static char buf[MAXFILENAME];

  fgets(buf, MAXFILENAME, stdin);
  buf[MAXFILENAME-1] = '\0';
  
  return buf;
}

#endif /* HAVE_READLINE */


char *spif_get_filename(void)
{
  char *name, *s;

  s = get_filename_line();
  for(; *s && isspace((int) *s); s++);
  name = s;
  for(; *s && isgraph((int) *s); s++);
  *s = '\0';

  if(name == s) {
    printf("Canceled!\n");
    return NULL;
  }

  return name;
}

char *spif_get_tape_fileinfo(int *startp, int *nump)
{
  char *name, *s;
  int res;
  
  s = get_filename_line();
  for(; *s && isspace((int) *s); s++);
  name = s;
  for(; *s && isgraph((int) *s); s++);
  
  if(name != s) res = 1;
  else res = 0;

  if(*s) {
    *s = '\0';
    s++;
    if(*s) {
      int r1;
      r1 = sscanf(s, "%d %d", startp, nump);
      if(r1 > 0) res += r1;
    }
  }

  if(res < 1) {
    printf("Canceled!\n");
    return NULL;
  }

  if(res < 2) *startp = -1;
  if(res < 3) *nump = -1;

  return name;
}

static int prevtmp = 0;

static void clear_line(void)
{
  if(prevtmp) {
    prevtmp = 0;
    fprintf(stderr, "                                                     \r");
  }
}

void put_msg(const char *msg)
{
// bjs - redundant for ODROID
//  if(spif_can_print) {
//    clear_line();
//    fprintf(stderr, "%s\n", msg);
//  }
}


void put_tmp_msg(const char *msg)
{
  if(spif_can_print) {
    clear_line();
    fprintf(stderr, "%s\r", msg);
    prevtmp = 1;
  }
}

