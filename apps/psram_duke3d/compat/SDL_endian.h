/*
 * SDL_endian.h compat header for Duke3D PSRAM app.
 */
#ifndef SDL_endian_h_
#define SDL_endian_h_

#define SDL_LIL_ENDIAN  1234
#define SDL_BIG_ENDIAN  4321
#define SDL_BYTEORDER   SDL_LIL_ENDIAN  /* RISC-V is little-endian */

#include <stdint.h>

static inline uint16_t SDL_Swap16(uint16_t x) {
    return (uint16_t)((x << 8) | (x >> 8));
}
static inline uint32_t SDL_Swap32(uint32_t x) {
    return __builtin_bswap32(x);
}

#if SDL_BYTEORDER == SDL_LIL_ENDIAN
#define SDL_SwapLE16(X) (X)
#define SDL_SwapLE32(X) (X)
#define SDL_SwapBE16(X) SDL_Swap16(X)
#define SDL_SwapBE32(X) SDL_Swap32(X)
#else
#define SDL_SwapLE16(X) SDL_Swap16(X)
#define SDL_SwapLE32(X) SDL_Swap32(X)
#define SDL_SwapBE16(X) (X)
#define SDL_SwapBE32(X) (X)
#endif

#endif /* SDL_endian_h_ */
