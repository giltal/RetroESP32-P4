/* SuperFX/GSU stub — SuperFX ROMs are not supported on ESP32-P4.
 * This file provides empty implementations so the rest of snes9x links. */

#include "snes9x.h"
#include "fxemu.h"
#include "fxinst.h"

struct FxInfo_s SuperFX;
FxRegs_s GSU;

void S9xInitSuperFX(void) {}
void S9xResetSuperFX(void) {}
void S9xSuperFXExec(void) {}
void S9xSetSuperFX(uint8_t byte, uint16_t address) { (void)byte; (void)address; }
uint8_t S9xGetSuperFX(uint16_t address) { (void)address; return 0; }
void fx_flushCache(void) {}
void fx_computeScreenPointers(void) {}
uint32_t fx_run(uint32_t nInstructions) { (void)nInstructions; return 0; }

/* Opcode/plot tables — unused but referenced by header */
void (* fx_OpcodeTable[1024])(void);
void (*fx_PlotTable[16])(void);
