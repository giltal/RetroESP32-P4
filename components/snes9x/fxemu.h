/* SuperFX/GSU emulation API — ported from snes9x upstream.
 * This header defines the FxInfo_s structure and the public API. */

#ifndef _FXEMU_H_
#define _FXEMU_H_

#include <stdint.h>
#include <stdbool.h>

#define FX_BREAKPOINT            (-1)
#define FX_ERROR_ILLEGAL_ADDRESS (-2)

/* The FxInfo_s structure — link between the FxEmulator and SNES emulator */
struct FxInfo_s
{
   uint32_t vFlags;
   uint8_t  *pvRegisters;   /* 768 bytes at memory address $3000        */
   uint32_t nRamBanks;      /* Number of 64KB banks in GSU-RAM ($70-$73)*/
   uint8_t  *pvRam;         /* Pointer to GSU-RAM                       */
   uint32_t nRomBanks;      /* Number of 32KB banks in Cart-ROM         */
   uint8_t  *pvRom;         /* Pointer to Cart-ROM                      */
   uint32_t speedPerLine;   /* GSU cycles to execute per scanline       */
   bool     oneLineDone;    /* True if GSU already ran this scanline    */
};

extern struct FxInfo_s SuperFX;

void    S9xInitSuperFX(void);
void    S9xResetSuperFX(void);
void    S9xSuperFXExec(void);
void    S9xSetSuperFX(uint8_t byte, uint16_t address);
uint8_t S9xGetSuperFX(uint16_t address);
void    fx_flushCache(void);
void    fx_computeScreenPointers(void);
uint32_t fx_run(uint32_t nInstructions);

#endif /* _FXEMU_H_ */
