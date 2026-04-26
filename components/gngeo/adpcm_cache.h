#ifndef ADPCM_CACHE_H
#define ADPCM_CACHE_H

#include <stdio.h>
#include <stdint.h>

/*
 * ADPCM V-ROM page cache for Neo Geo.
 * Provides random-access byte reads from SD-card-backed V-ROM files
 * via a PSRAM page cache with direct-mapped lookup.
 *
 * Uses a direct-mapped cache: page_num % num_slots → slot index.
 * Conflicts evict the previous occupant. Simple and O(1) lookup.
 */

#define ADPCM_PAGE_SHIFT  12        /* 4 KB pages */
#define ADPCM_PAGE_SIZE   (1 << ADPCM_PAGE_SHIFT)
#define ADPCM_PAGE_MASK   (ADPCM_PAGE_SIZE - 1)

typedef struct adpcm_cache {
    uint8_t  *data;         /* Cache buffer in PSRAM */
    uint32_t  num_slots;    /* Number of cache page slots */
    int      *slot_tag;     /* slot_tag[slot] = which ROM page is here (-1 = empty) */
    FILE     *file;         /* Backing file on SD card */
    uint32_t  file_size;    /* Total ROM size */
    int       active;       /* 1 = cache is active, 0 = direct PSRAM pointer */
} adpcm_cache_t;

/* Initialize the ADPCM cache.
 * Returns 0 on success, -1 on failure */
int adpcm_cache_init(adpcm_cache_t *c, FILE *file, uint32_t file_size, uint32_t cache_bytes);

/* Free all cache resources */
void adpcm_cache_free(adpcm_cache_t *c);

/* Cache miss handler (not inline — called rarely) */
uint8_t adpcm_cache_miss(adpcm_cache_t *c, uint32_t addr, uint32_t page_num, uint32_t slot);

/* Read a single byte at the given ROM address — inline for hot path */
static inline uint8_t adpcm_cache_read(adpcm_cache_t *c, uint32_t addr) {
    uint32_t page_num = addr >> ADPCM_PAGE_SHIFT;
    uint32_t page_off = addr & ADPCM_PAGE_MASK;
    uint32_t slot = page_num % c->num_slots;

    if (c->slot_tag[slot] == (int)page_num) {
        /* Cache hit */
        return c->data[slot * ADPCM_PAGE_SIZE + page_off];
    }
    /* Cache miss — load from SD */
    return adpcm_cache_miss(c, addr, page_num, slot);
}

/* Global ADPCM cache instances (defined in ym2610.c) */
extern adpcm_cache_t adpcm_cacheA;
extern adpcm_cache_t adpcm_cacheB;

#endif /* ADPCM_CACHE_H */
