/* SuperFX/GSU instruction definitions — ported from snes9x upstream.
 * Converted to C with this fork's type system (C99 stdint). */

#ifndef _FXINST_H_
#define _FXINST_H_

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <esp_attr.h>

/* Number of 64KB GSU-RAM banks (banks $70-$73). 4 banks = 256KB. */
#define FX_RAM_BANKS 4

/* --- GSU status/flag register bits --- */
#define FLG_G   (1 << 5)   /* GO flag — set when GSU is running          */
#define FLG_IRQ (1 << 15)  /* IRQ flag — set when GSU wants to interrupt */
#define FLG_Z   (1 << 1)   /* Zero flag                                  */
#define FLG_S   (1 << 3)   /* Sign flag                                  */
#define FLG_OV  (1 << 4)   /* Overflow flag                              */
#define FLG_CY  (1 << 2)   /* Carry flag                                 */
#define FLG_ALT1 (1 << 8)  /* ALT1 flag                                  */
#define FLG_ALT2 (1 << 9)  /* ALT2 flag                                  */
#define FLG_B   (1 << 12)  /* B flag (WITH prefix active)                */
#define FLG_IL  (1 << 10)  /* Immediate-lower flag                       */
#define FLG_IH  (1 << 11)  /* Immediate-high flag                        */

/* --- GSU register offsets in the 768-byte register space ($3000-$32FF) --- */
#define GSU_R0     0x000
#define GSU_R1     0x002
#define GSU_R2     0x004
#define GSU_R3     0x006
#define GSU_R4     0x008
#define GSU_R5     0x00a
#define GSU_R6     0x00c
#define GSU_R7     0x00e
#define GSU_R8     0x010
#define GSU_R9     0x012
#define GSU_R10    0x014
#define GSU_R11    0x016
#define GSU_R12    0x018
#define GSU_R13    0x01a
#define GSU_R14    0x01c
#define GSU_R15    0x01e
#define GSU_SFR    0x030
#define GSU_BRAMR  0x033
#define GSU_PBR    0x034
#define GSU_ROMBR  0x036
#define GSU_CFGR   0x037
#define GSU_SCBR   0x038
#define GSU_CLSR   0x039
#define GSU_SCMR   0x03a
#define GSU_VCR    0x03b
#define GSU_RAMBR  0x03c
#define GSU_CBR    0x03e

/* --- Useful shorthands --- */
#define USEX8(a)   ((uint32_t) ((uint8_t)  (a)))
#define USEX16(a)  ((uint32_t) ((uint16_t) (a)))
#define SEX8(a)    ((int32_t)  ((int8_t)   (a)))
#define SEX16(a)   ((int32_t)  ((int16_t)  (a)))
#define SUSEX16(a) ((int32_t)  ((int16_t)  (a)))

/* Shorthands for common register references */
#define R0   GSU.avReg[0]
#define R1   GSU.avReg[1]
#define R2   GSU.avReg[2]
#define R3   GSU.avReg[3]
#define R4   GSU.avReg[4]
#define R5   GSU.avReg[5]
#define R6   GSU.avReg[6]
#define R7   GSU.avReg[7]
#define R8   GSU.avReg[8]
#define R9   GSU.avReg[9]
#define R10  GSU.avReg[10]
#define R11  GSU.avReg[11]
#define R12  GSU.avReg[12]
#define R13  GSU.avReg[13]
#define R14  GSU.avReg[14]
#define R15  GSU.avReg[15]

#define SREG (*GSU.pvSreg)
#define DREG (*GSU.pvDreg)

#define COLR GSU.vColorReg

/* --- Flag access macros --- */
#define SF(a)  (GSU.vStatusReg |=  FLG_##a)
#define CF(a)  (GSU.vStatusReg &= ~FLG_##a)
#define TF(a)  (GSU.vStatusReg &   FLG_##a)

/* --- Clear instruction prefix flags after each opcode --- */
#define CLRFLAGS do { \
   GSU.vStatusReg &= ~(FLG_ALT1 | FLG_ALT2 | FLG_B); \
   GSU.pvDreg = GSU.pvSreg = &R0; \
} while(0)

/* --- ROM/RAM access macros --- */
#ifndef FX_DO_ROMBUFFER
#define ROM(idx) (GSU.pvRomBank[(idx) & 0xFFFF])
#else
#define ROM(idx) (GSU.pvRomBank[(idx) & 0xFFFF])
#endif

#define RAM(idx) (GSU.pvRamBank[(idx) & 0xFFFF])

/* Code ROM via program bank (PBR) — for instruction fetches */
#define CODE_ROM(idx) (GSU.pvPrgBank[(idx) & 0xFFFF])

/* --- Pixel plot access --- */
#define SCMR  (GSU.pvRegisters[GSU_SCMR])

/* --- Pipeline fetch --- */
#define PIPE  GSU.vPipe

#define FETCHPIPE do { \
   PIPE = CODE_ROM(R15); \
} while(0)

/* --- R14 ROM read buffer update --- */
#define READR14 do { \
   GSU.vRomBuffer = ROM(R14); \
} while(0)

/* --- Destination register R14 side-effect check --- */
#define TESTR14 do { \
   if (GSU.pvDreg == &R14) READR14; \
} while(0)

/* --- Step macro: decode + execute one GSU opcode ---
 * FX_STEP reads opcode from CODE_ROM(R15), advances R15 past opcode,
 * then dispatches. Handlers advance R15 only for operand bytes. */
#define FX_STEP do { \
   uint8_t _op = CODE_ROM(R15); \
   R15++; \
   (*fx_OpcodeTable[(_op | (GSU.vStatusReg & 0x300))])(); \
} while(0)

/* --- GSU internal register file --- */
typedef struct
{
   /* 16 general-purpose 16-bit registers */
   uint32_t  avReg[16];

   /* Source and destination register pointers */
   uint32_t  *pvSreg;
   uint32_t  *pvDreg;

   /* Status register variables (unpacked for speed) */
   uint32_t  vStatusReg;
   uint32_t  vZero;
   uint32_t  vSign;
   int32_t   vOverflow;
   uint32_t  vCarry;

   /* Colour register */
   uint8_t   vColorReg;

   /* Plot option register */
   uint8_t   vPlotOptionReg;

   /* Program bank register */
   uint32_t  vPrgBankReg;

   /* ROM bank register */
   uint32_t  vRomBankReg;

   /* RAM bank register */
   uint32_t  vRamBankReg;

   /* Cache base register */
   uint32_t  vCacheBaseReg;

   /* Pipeline byte */
   uint8_t   vPipe;

   /* Pointers to banks */
   uint8_t   *pvRegisters;      /* 768 bytes at $3000 */
   uint8_t   *pvRam;            /* GSU RAM */
   uint8_t   *pvRom;            /* Cart ROM */
   uint32_t  nRamBanks;
   uint32_t  nRomBanks;

   /* Bank mapping tables */
   uint8_t   *apvRomBank[256];
   uint8_t   *apvRamBank[4];

   /* Current bank pointers */
   uint8_t   *pvRomBank;
   uint8_t   *pvRamBank;
   uint8_t   *pvPrgBank;

   /* Screen rendering variables */
   uint8_t   *pvScreenBase;
   uint8_t   *apvScreen[32];
   uint32_t  x[32];
   uint32_t  vScreenHeight;
   uint32_t  vScreenRealHeight;
   uint32_t  vScreenSize;
   uint32_t  vMode;
   uint32_t  vPrevMode;
   uint32_t  vPrevScreenHeight;
   bool      vSCBRDirty;

   /* Pixel plot function pointers */
   void      (*pfPlot)(void);
   void      (*pfRpix)(void);

   /* Cache */
   uint8_t   *pvCache;
   uint32_t  vCacheFlags;
   bool      bCacheActive;

   /* Counters */
   uint32_t  vCounter;
   uint32_t  vInstCount;

   /* ROM read buffer */
   uint8_t   vRomBuffer;

   /* Last RAM address (for SBK instruction) */
   uint32_t  vLastRamAdr;

   /* Error code */
   int32_t   vErrorCode;

   /* Breakpoint */
   bool      bBreakPoint;
   uint32_t  vBreakPoint;
   uint32_t  vStepPoint;

   /* Cache backup (not used in this port) */
   /* uint8_t avCacheBackup[512]; */
} FxRegs_s;

extern FxRegs_s GSU;

/* Opcode tables (ALT0, ALT1, ALT2, ALT3 — 4 × 256 = 1024 entries) */
extern void (* DRAM_ATTR fx_OpcodeTable[])(void);

/* Plot/RPix function tables */
extern void (*fx_PlotTable[])(void);

/* Public functions used by fxemu.c */
void fx_flushCache(void);
void fx_computeScreenPointers(void);
uint32_t fx_run(uint32_t nInstructions);

#endif /* _FXINST_H_ */
