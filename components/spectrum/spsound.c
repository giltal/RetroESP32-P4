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

/* #define DEBUG_AUDIO */

#include "spsound.h"
#include "config.h"
#include "spperif.h"
#include "z80.h"
#include "misc.h"
#include "interf.h"

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "esp_system.h"
#include "odroid_audio.h"
#include "ay_sound.h"

#define VOLREDUCE 2

int bufframes = 4;

int sound_avail = 0;
int sound_on = 1;

int sound_to_autoclose = 1;
int sound_sample_rate = 0;
int sound_dsp_setfrag = 1;

uint16_t* audioBuf = NULL;

#ifdef HAVE_SOUND

#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <errno.h>
#include <string.h>

#include <sys/types.h>
#include <sys/time.h>

//#define SKIPTIME 5000
#define SKIPTIME 20000 // 50fps, 20ms per frame, or 20,000us


#define AUTOCLOSET 5
static int autocloset;

#define REOPENT 5
static int opent = 0;
static int last_not_played;

#define SPS_OPENED        0
#define SPS_AUTOCLOSED	 -1
#define SPS_BUSY	 -2
#define SPS_CLOSED	 -3
#define SPS_NONEXIST	 -4

static int sndstate = SPS_CLOSED;

static byte open_buf[TMNUM];

static void close_snd(int normal);

const byte lin8_ulaw[] = {
    31,   31,   31,   32,   32,   32,   32,   33, 
    33,   33,   33,   34,   34,   34,   34,   35, 
    35,   35,   35,   36,   36,   36,   36,   37, 
    37,   37,   37,   38,   38,   38,   38,   39, 
    39,   39,   39,   40,   40,   40,   40,   41, 
    41,   41,   41,   42,   42,   42,   42,   43, 
    43,   43,   43,   44,   44,   44,   44,   45, 
    45,   45,   45,   46,   46,   46,   46,   47, 
    47,   47,   47,   48,   48,   49,   49,   50, 
    50,   51,   51,   52,   52,   53,   53,   54, 
    54,   55,   55,   56,   56,   57,   57,   58, 
    58,   59,   59,   60,   60,   61,   61,   62, 
    62,   63,   63,   64,   65,   66,   67,   68, 
    69,   70,   71,   72,   73,   74,   75,   76, 
    77,   78,   79,   81,   83,   85,   87,   89, 
    91,   93,   95,   99,  103,  107,  111,  119, 
   255,  247,  239,  235,  231,  227,  223,  221, 
   219,  217,  215,  213,  211,  209,  207,  206, 
   205,  204,  203,  202,  201,  200,  199,  198, 
   197,  196,  195,  194,  193,  192,  191,  191, 
   190,  190,  189,  189,  188,  188,  187,  187, 
   186,  186,  185,  185,  184,  184,  183,  183, 
   182,  182,  181,  181,  180,  180,  179,  179, 
   178,  178,  177,  177,  176,  176,  175,  175, 
   175,  175,  174,  174,  174,  174,  173,  173, 
   173,  173,  172,  172,  172,  172,  171,  171, 
   171,  171,  170,  170,  170,  170,  169,  169, 
   169,  169,  168,  168,  168,  168,  167,  167, 
   167,  167,  166,  166,  166,  166,  165,  165, 
   165,  165,  164,  164,  164,  164,  163,  163, 
   163,  163,  162,  162,  162,  162,  161,  161, 
   161,  161,  160,  160,  160,  160,  159,  159, 
};

void write_generic(int numsam)
{
}


/* -------- Open Sound System support -------- */

#undef OSS_SOUND
#ifdef OSS_SOUND

#include <sys/soundcard.h>
#include <sys/ioctl.h>

#define VOLREDUCE 2

static int buffrag = 8;

static void close_snd(int normal)
{
  if(sndstate >= SPS_OPENED && normal) {
    int res;

    res = ioctl(snd,SOUND_PCM_SYNC,0);
    if(res < 0) {
      sprintf(msgbuf, "ioctl(SOUND_PCM_WRITE_SYNC, 0) failed: %s",
	      strerror(errno));
      put_msg(msgbuf);
    }
  }

  close_generic();
}

#undef FRAG
#define FRAG(x, y) ((((x) & 0xFFFF) << 16) | ((y) & 0xFFFF))

#endif /* OSS_SOUND */

#define CONVU8(x) ((byte) (((x) >> VOLREDUCE) + 128))

#ifdef CONVERT_TO_ULAW
#  define CONV(x) lin8_ulaw[(int) CONVU8(x)]
#else
#  define CONV(x) CONVU8(x)
#endif

#define HIGH_PASS(hp, sv) (((hp) * 15 + (sv)) >> 4)
#define TAPESOUND(tsp)    ((*tsp) >> 4)

void process_sound(void)
{
  static int soundhp;
  int i;
  byte *sb;
  register int sv;

  /* sp_sound_buf is Unsigned 8 bit, Rate 8000 Hz, Mono */
  sb = sp_sound_buf;
  if(last_not_played) {
    soundhp = *sb;
    last_not_played = 0;
  }

  if(!sp_playing_tape) {

    for(i = TMNUM; i; sb++,i--) {
      sv = *sb;
      soundhp = HIGH_PASS(soundhp, sv);
      *sb = ((CONV(sv - soundhp)));
    }
  }
  else {
    signed char *tsp;

    tsp = sp_tape_sound;
    for(i = TMNUM; i; sb++,tsp++,i--) {
      sv = *sb + TAPESOUND(tsp);
      soundhp = (soundhp * 15 + sv)>>4;
      *sb = CONV(sv - soundhp);
    }
  }

//djk
//------------------------------------------
  //convert to 16bit stereo buffer for odroid_common to process

  if (!audioBuf)
    {
      size_t bufferSize = TMNUM * 2 * sizeof(short);
      audioBuf = heap_caps_malloc(bufferSize, MALLOC_CAP_8BIT | MALLOC_CAP_DMA);
    }

  /* Render AY-3-8912 PSG into a temporary mono buffer */
  static int16_t ay_buf[TMNUM];
  ay_render(ay_buf, TMNUM);

  /* Mix beeper + AY into stereo output */
  for (short i = 0; i < TMNUM; i++)
  {
    int32_t beeper = sp_sound_buf[i];
    beeper -= 128;              /* unsigned 8-bit → signed */
    beeper <<= 8;               /* scale to 16-bit range */

    int32_t mixed = beeper + (int32_t)ay_buf[i];

    /* Clamp to 16-bit range */
    if (mixed > 32767) mixed = 32767;
    if (mixed < -32768) mixed = -32768;

    int16_t s16 = (int16_t)mixed;
    audioBuf[i*2]     = s16;    /* Left channel */
    audioBuf[i*2 + 1] = s16;   /* Right channel (mono dup) */
  }

  odroid_audio_submit((short*)audioBuf,TMNUM);

//--------------------------------------------

}

void autoclose_sound(void)
{
  if(sound_on && sound_to_autoclose && sndstate >= SPS_CLOSED) {
#ifdef DEBUG_AUDIO
    fprintf(stderr, "Autoclosing sound\n");
#endif
    //close_snd(1);
    sndstate = SPS_AUTOCLOSED;
  }
}

void play_sound(int evenframe)
{
  time_t nowt;
  int snd_change;

  if(evenframe) return;

  if(sndstate <= SPS_NONEXIST) return;

  if(!sound_on) {
    if(sndstate <= SPS_CLOSED) return;
    if(sndstate < SPS_OPENED) {
      sndstate = SPS_CLOSED;
      return;
    }
   // close_snd(1);
    return;
  }

  z80_proc.sound_change = 0;

  process_sound();

}

#else /* HAVE_SOUND */

/* Dummy functions */

void setbufsize(void)
{
}

void init_spect_sound(void)
{
}

void play_sound(int evenframe)
{
  evenframe = evenframe;
}

void autoclose_sound(void)
{
}

#endif /* NO_SOUND */

