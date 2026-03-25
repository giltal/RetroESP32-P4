/* Stub */
#ifndef PAPP_COMPAT_ESP_HEAP_CAPS_H
#define PAPP_COMPAT_ESP_HEAP_CAPS_H
#include <stddef.h>
#define MALLOC_CAP_SPIRAM    (1 << 10)
#define MALLOC_CAP_INTERNAL  (1 << 11)
#define MALLOC_CAP_8BIT      (1 << 13)
#define MALLOC_CAP_DMA       (1 << 2)
/* Declaration only — implementation in papp_syscalls.c */
void *heap_caps_malloc(size_t size, uint32_t caps);
void heap_caps_free(void *ptr);
#endif
