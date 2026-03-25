/*
 * pngAux - PNG helper for ESP-IDF
 * Ported from Arduino version to use standard C file I/O via VFS
 */
#include "pngAux.h"
#include <PNGdec.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_heap_caps.h"
#include "esp_log.h"

static const char *TAG = "pngAux";

// File handle for PNG callbacks
static FILE *_myfile = NULL;

static PNG _png;
static unsigned int _PNGendianessFlag;

// PNG file callbacks using standard C I/O (VFS)
static void* myOpen(const char *filename, int32_t *size)
{
    ESP_LOGI(TAG, "Attempting to open %s", filename);
    _myfile = fopen(filename, "rb");
    if (_myfile == NULL) {
        ESP_LOGE(TAG, "Failed to open file: %s", filename);
        *size = 0;
        return NULL;
    }
    fseek(_myfile, 0, SEEK_END);
    *size = (int32_t)ftell(_myfile);
    fseek(_myfile, 0, SEEK_SET);
    ESP_LOGI(TAG, "File opened, size: %ld bytes", (long)*size);
    return _myfile;
}

static void myClose(void *handle)
{
    if (_myfile) {
        fclose(_myfile);
        _myfile = NULL;
    }
}

static int32_t myRead(PNGFILE *handle, uint8_t *buffer, int32_t length)
{
    if (!_myfile) return 0;
    return (int32_t)fread(buffer, 1, length, _myfile);
}

static int32_t mySeek(PNGFILE *handle, int32_t position)
{
    if (!_myfile) return 0;
    return (int32_t)fseek(_myfile, position, SEEK_SET);
}

// Function to draw pixels to buffer
static uint16_t *_pngBuffer;

static void PNGDraw(PNGDRAW *pDraw)
{
    _png.getLineAsRGB565(pDraw, _pngBuffer, _PNGendianessFlag, 0xffffffff);
    _pngBuffer += _png.getWidth();
}

bool loadPngFromFile(const char *fileName, pngObject *pngObj, bool usePSRAM, bool littleEndian)
{
    int pngStatus;

    if (littleEndian) {
        _PNGendianessFlag = PNG_RGB565_LITTLE_ENDIAN;
    } else {
        _PNGendianessFlag = PNG_RGB565_BIG_ENDIAN;
    }

    pngStatus = _png.open(fileName, myOpen, myClose, myRead, mySeek, PNGDraw);
    if (pngStatus == PNG_SUCCESS) {
        ESP_LOGI(TAG, "Image specs: (%d x %d), %d bpp, pixel type: %d",
                 _png.getWidth(), _png.getHeight(), _png.getBpp(), _png.getPixelType());

        size_t buf_size = _png.getWidth() * _png.getHeight() * 2;
        if (usePSRAM) {
            _pngBuffer = (uint16_t *)heap_caps_malloc(buf_size + 4, MALLOC_CAP_SPIRAM);
            // Align to 4 bytes
            uintptr_t address = (uintptr_t)_pngBuffer;
            uintptr_t aligned_address = (address + 3) & ~3;
            _pngBuffer = (uint16_t *)aligned_address;
        } else {
            _pngBuffer = (uint16_t *)malloc(buf_size);
        }

        if (_pngBuffer == NULL) {
            ESP_LOGE(TAG, "Failed to allocate pixel buffer (%d bytes)", (int)buf_size);
            _png.close();
            return false;
        }

        pngObj->w = _png.getWidth();
        pngObj->h = _png.getHeight();
        pngObj->data = (unsigned int *)_pngBuffer;
        pngStatus = _png.decode(NULL, 0);

        // Color channel swap for ESP32-P4 MIPI DSI panel (BGR to RGB)
        uint16_t *tempPointer = (uint16_t *)pngObj->data;
        uint16_t tempColor, r, g, b;
        for (size_t i = 0; i < (size_t)_png.getWidth() * _png.getHeight(); i++) {
            tempColor = tempPointer[i];
            b = (tempColor & 0xf8) << 11;
            r = tempColor >> 11;
            g = ((tempColor & 0x07e0));
            tempPointer[i] = r | g | b;
        }

        _png.close();
        ESP_LOGI(TAG, "PNG decoded successfully: %dx%d", pngObj->w, pngObj->h);
        return true;
    } else {
        ESP_LOGE(TAG, "Failed to open PNG file: %s (status=%d)", fileName, pngStatus);
        return false;
    }
}

bool loadPngFromFileRaw(const char *fileName, pngObject *pngObj, bool usePSRAM, bool littleEndian)
{
    int pngStatus;

    if (littleEndian) {
        _PNGendianessFlag = PNG_RGB565_LITTLE_ENDIAN;
    } else {
        _PNGendianessFlag = PNG_RGB565_BIG_ENDIAN;
    }

    pngStatus = _png.open(fileName, myOpen, myClose, myRead, mySeek, PNGDraw);
    if (pngStatus == PNG_SUCCESS) {
        ESP_LOGI(TAG, "Image specs: (%d x %d), %d bpp, pixel type: %d",
                 _png.getWidth(), _png.getHeight(), _png.getBpp(), _png.getPixelType());

        size_t buf_size = _png.getWidth() * _png.getHeight() * 2;
        if (usePSRAM) {
            _pngBuffer = (uint16_t *)heap_caps_malloc(buf_size + 4, MALLOC_CAP_SPIRAM);
            uintptr_t address = (uintptr_t)_pngBuffer;
            uintptr_t aligned_address = (address + 3) & ~3;
            _pngBuffer = (uint16_t *)aligned_address;
        } else {
            _pngBuffer = (uint16_t *)malloc(buf_size);
        }

        if (_pngBuffer == NULL) {
            ESP_LOGE(TAG, "Failed to allocate pixel buffer (%d bytes)", (int)buf_size);
            _png.close();
            return false;
        }

        pngObj->w = _png.getWidth();
        pngObj->h = _png.getHeight();
        pngObj->data = (unsigned int *)_pngBuffer;
        pngStatus = _png.decode(NULL, 0);

        /* No color swap — raw RGB565 as decoded by PNGdec */

        _png.close();
        ESP_LOGI(TAG, "PNG decoded (raw) successfully: %dx%d", pngObj->w, pngObj->h);
        return true;
    } else {
        ESP_LOGE(TAG, "Failed to open PNG file: %s (status=%d)", fileName, pngStatus);
        return false;
    }
}
