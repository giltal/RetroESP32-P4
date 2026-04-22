/*
 * zlib.h shim — redirect to stb_zlib for ESP32-P4
 * GnGeo's state.c and video.c use gzopen/gzread/gzwrite/gzclose.
 * We stub these since state save can use raw fwrite for now.
 */
#ifndef _ZLIB_SHIM_H_
#define _ZLIB_SHIM_H_

#include <stdio.h>
#include <stdint.h>

typedef unsigned long uLong;
typedef unsigned long uLongf;
typedef unsigned char Bytef;

typedef FILE *gzFile;

static inline gzFile gzopen(const char *path, const char *mode) {
    return fopen(path, mode);
}

static inline int gzread(gzFile file, void *buf, unsigned len) {
    return (int)fread(buf, 1, len, file);
}

static inline int gzwrite(gzFile file, const void *buf, unsigned len) {
    return (int)fwrite(buf, 1, len, file);
}

static inline int gzclose(gzFile file) {
    return fclose(file);
}

static inline int gzeof(gzFile file) {
    return feof(file);
}

static inline const char *gzerror(gzFile file, int *errnum) {
    (void)file;
    if (errnum) *errnum = 0;
    return "";
}

/* zlib uncompress — implemented in esp32_platform.c via stb_zlib */
int uncompress(uint8_t *dest, unsigned long *destLen,
               const uint8_t *source, unsigned long sourceLen);

/* compressBound macro */
#ifndef compressBound
#define compressBound(sourceLen) ((sourceLen) + ((sourceLen) >> 12) + ((sourceLen) >> 14) + ((sourceLen) >> 25) + 13)
#endif

#define Z_OK 0

#endif /* _ZLIB_SHIM_H_ */
