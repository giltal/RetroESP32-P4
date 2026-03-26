/* Stub heap_caps for PSRAM app build */
#ifndef PAPP_COMPAT_ESP_HEAP_CAPS_H
#define PAPP_COMPAT_ESP_HEAP_CAPS_H

#include <stddef.h>
#include <stdint.h>

#define MALLOC_CAP_DMA       (1 << 2)
#define MALLOC_CAP_SPIRAM    (1 << 10)
#define MALLOC_CAP_INTERNAL  (1 << 11)
#define MALLOC_CAP_8BIT      (1 << 0)
#define MALLOC_CAP_32BIT     (1 << 1)

void *heap_caps_malloc(size_t size, uint32_t caps);
void  heap_caps_free(void *ptr);

static inline void *heap_caps_calloc(size_t n, size_t size, uint32_t caps)
{
    void *p = heap_caps_malloc(n * size, caps);
    if (p) __builtin_memset(p, 0, n * size);
    return p;
}

#endif
