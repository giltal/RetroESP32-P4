/*
 * pngAux - PNG helper for ESP-IDF
 * Ported from Arduino version to use standard C file I/O (VFS)
 */
#ifndef _PNGAUX_H_INCLUDED
#define _PNGAUX_H_INCLUDED

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct _pngObject
{
    unsigned short w, h;
    unsigned int* data;
} pngObject;

/**
 * @brief Load a PNG file from the SD card (mounted via VFS) and decode to RGB565 in PSRAM
 *
 * @param fileName Full VFS path to the PNG file (e.g., "/sdcard/breakout.png")
 * @param pngObj Pointer to pngObject to fill with decoded data
 * @param usePSRAM If true, allocate pixel buffer in PSRAM
 * @param littleEndian If true, use little-endian RGB565 byte order
 * @return true on success, false on failure
 */
bool loadPngFromFile(const char *fileName, pngObject *pngObj, bool usePSRAM, bool littleEndian);

/**
 * @brief Same as loadPngFromFile but skips the BGR→RGB channel swap.
 *        Use when writing directly to the LCD panel (no emulator palette).
 */
bool loadPngFromFileRaw(const char *fileName, pngObject *pngObj, bool usePSRAM, bool littleEndian);

#ifdef __cplusplus
}
#endif

#endif
