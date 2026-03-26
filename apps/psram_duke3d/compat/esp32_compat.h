/*
 * esp32_compat.h compat header for Duke3D PSRAM app.
 * Included by platform.h and global.h — defines platform macros
 * and stdint types that the engine expects.
 */
#ifndef ESP32_COMPAT_H
#define ESP32_COMPAT_H

#include <stdint.h>
#include <stdlib.h>
#include "esp_attr.h"

/* Platform byte order — RISC-V is little-endian */
#ifndef LITTLE_ENDIAN
#define LITTLE_ENDIAN 1234
#endif
#ifndef BIG_ENDIAN
#define BIG_ENDIAN 4321
#endif
#ifndef BYTE_ORDER
#define BYTE_ORDER LITTLE_ENDIAN
#endif

/* O_BINARY is a DOS/Windows flag, not available on POSIX */
#ifndef O_BINARY
#define O_BINARY 0
#endif

/* strcmpi → strcasecmp mapping */
#include <strings.h>
#define strcmpi strcasecmp
#define stricmp strcasecmp

/* putenv / getenv stubs */
#define SDL_putenv(x) 0

/* Path separator — Unix/ESP32 style */
#ifndef PATH_SEP_CHAR
#define PATH_SEP_CHAR '/'
#endif
#ifndef PATH_SEP_STR
#define PATH_SEP_STR "/"
#endif
#ifndef CURDIR
#define CURDIR "./"
#endif

/* F_OK — access() mode for existence check */
#include <unistd.h>

/* mkdir — game.c calls mkdir(path) with 1 arg (DOS style).
   Must include sys/stat.h BEFORE the mkdir macro so the system
   declaration is processed without macro interference. */
#include <sys/stat.h>
static inline int _duke_mkdir(const char *path) { return mkdir(path, 0755); }
#define mkdir(path) _duke_mkdir(path)

/* min/max — used by engine.c (not provided by newlib) */
#ifndef min
#define min(a,b) (((a)<(b))?(a):(b))
#endif
#ifndef max
#define max(a,b) (((a)>(b))?(a):(b))
#endif

/* FP_OFF — DOS near-pointer offset. Flat memory: just cast to uintptr_t. */
#ifndef FP_OFF
#define FP_OFF(p) ((unsigned)(uintptr_t)(p))
#endif

/* __int64 — MSVC type, map to standard int64_t on GCC */
#ifndef __int64
#define __int64 long long
#endif

/* DOS date struct used by _dos_getdate in global.c */
struct dosdate_t {
    unsigned char day;
    unsigned char month;
    unsigned short year;
    unsigned char dayofweek;
};

/* DOS find_t struct used by global.c filesystem functions */
#include <dirent.h>
struct find_t {
    char pattern[256];
    char name[256];
    DIR *dir;
};

int _dos_findfirst(char *filename, int x, struct find_t *f);
int _dos_findnext(struct find_t *f);

#endif
