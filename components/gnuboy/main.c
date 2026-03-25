/*
 * gnuboy minimal runtime support
 * Only die() is needed — the entry point is gnuboy_run() in gnuboy_run.c
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

#include "gnuboy.h"

void die(char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	abort();
}

/* Stubs for functions referenced by gnuboy core but not used in our port */
void doevents(void) { }
void vid_preinit(void) { }
void vid_init(void) { }
void vid_close(void) { }
void pcm_init(void) { }
void pcm_close(void) { }
