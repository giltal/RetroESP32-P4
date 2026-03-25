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

#include "sptiming.h"
#include "interf.h"


#include <sys/time.h>
#include <unistd.h>
#include "esp_system.h"

static void add_tv(long usec, struct timeval *tv)
{
  long us1;

  us1 = tv->tv_usec + usec;
  tv->tv_sec += us1 / 1000000;
  tv->tv_usec = us1 % 1000000;
}

static long sub_tv(struct timeval *tv1, struct timeval *tv2)
{
  return ((tv1->tv_sec - tv2->tv_sec) * 1000000 + 
	  (tv1->tv_usec - tv2->tv_usec));
}

void spti_init(void)
{
}

void spti_sleep(unsigned long usecs)
{
  struct timeval waittv;
 
  waittv.tv_sec = 0;
  waittv.tv_usec = usecs;
  select(0, NULL, NULL, NULL, &waittv);
}

static struct timeval shouldbetv;

void spti_reset(void)
{
  gettimeofday(&shouldbetv, NULL);
}

void spti_wait(void)
{
  long rem;
  struct timeval nowtv;

  add_tv(SKIPTIME, &shouldbetv);
  
  gettimeofday(&nowtv, NULL);
  rem = sub_tv(&shouldbetv, &nowtv);
  if(rem > 0) {
    if(rem > SKIPTIME) rem = SKIPTIME;
    spti_sleep((unsigned long) rem);
  }
  
  if(rem == SKIPTIME || rem < -10 * SKIPTIME) spti_reset();
}
