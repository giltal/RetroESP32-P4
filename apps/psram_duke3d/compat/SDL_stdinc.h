/*
 * SDL_stdinc.h compat header for Duke3D PSRAM app.
 * Provides base types: Uint8, Sint16, etc.
 */
#ifndef SDL_stdinc_h_
#define SDL_stdinc_h_

#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <stdarg.h>
#include <stdint.h>
#include <string.h>
#include <ctype.h>
#include <math.h>

#define SDL_arraysize(array) (sizeof(array)/sizeof(array[0]))
#define SDL_TABLESIZE(table) SDL_arraysize(table)
#define SDL_STRINGIFY_ARG(arg) #arg

#define SDL_reinterpret_cast(type, expression) ((type)(expression))
#define SDL_static_cast(type, expression) ((type)(expression))
#define SDL_const_cast(type, expression) ((type)(expression))

#define SDL_FOURCC(A, B, C, D) \
    ((SDL_static_cast(uint32_t, SDL_static_cast(uint8_t, (A))) << 0) | \
     (SDL_static_cast(uint32_t, SDL_static_cast(uint8_t, (B))) << 8) | \
     (SDL_static_cast(uint32_t, SDL_static_cast(uint8_t, (C))) << 16) | \
     (SDL_static_cast(uint32_t, SDL_static_cast(uint8_t, (D))) << 24))

typedef enum { SDL_FALSE = 0, SDL_TRUE = 1 } SDL_bool;

typedef int8_t   Sint8;
typedef uint8_t  Uint8;
typedef int16_t  Sint16;
typedef uint16_t Uint16;
typedef int32_t  Sint32;
typedef uint32_t Uint32;
typedef int64_t  Sint64;
typedef uint64_t Uint64;

#define SDL_MAX_SINT8  ((Sint8)0x7F)
#define SDL_MIN_SINT8  ((Sint8)(~0x7F))
#define SDL_MAX_UINT8  ((Uint8)0xFF)
#define SDL_MIN_UINT8  ((Uint8)0x00)
#define SDL_MAX_SINT16 ((Sint16)0x7FFF)
#define SDL_MIN_SINT16 ((Sint16)(~0x7FFF))
#define SDL_MAX_UINT16 ((Uint16)0xFFFF)
#define SDL_MIN_UINT16 ((Uint16)0x0000)
#define SDL_MAX_SINT32 ((Sint32)0x7FFFFFFF)
#define SDL_MIN_SINT32 ((Sint32)(~0x7FFFFFFF))
#define SDL_MAX_UINT32 ((Uint32)0xFFFFFFFFu)
#define SDL_MIN_UINT32 ((Uint32)0x00000000)

#define DECLSPEC
#define SDLCALL

#endif /* SDL_stdinc_h_ */
