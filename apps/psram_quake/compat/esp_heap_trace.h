/* Stub */
#ifndef PAPP_COMPAT_ESP_HEAP_TRACE_H
#define PAPP_COMPAT_ESP_HEAP_TRACE_H

typedef struct { int dummy; } heap_trace_record_t;

typedef enum {
    HEAP_TRACE_LEAKS = 0,
    HEAP_TRACE_ALL = 1
} heap_trace_mode_t;

static inline int heap_trace_init_standalone(heap_trace_record_t *buf, size_t n) { return 0; }
static inline int heap_trace_start(heap_trace_mode_t mode) { return 0; }
static inline int heap_trace_stop(void) { return 0; }
static inline void heap_trace_dump(void) {}

#endif
