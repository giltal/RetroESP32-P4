#ifndef M68K__HEADER
#define M68K__HEADER

/* ======================================================================== */
/* ========================= LICENSING & COPYRIGHT ======================== */
/* ======================================================================== */
/*
 *                                  MUSASHI
 *                                Version 3.32
 *
 * A portable Motorola M680x0 processor emulation engine.
 * Copyright Karl Stenerud.  All rights reserved.
 *
 * This code may be freely used for non-commercial purposes as long as this
 * copyright notice remains unaltered in the source code and any binary files
 * containing this code in compiled form.
 *
 * All other licensing terms must be negotiated with the author
 * (Karl Stenerud).
 *
 * The latest version of this code can be obtained at:
 * http://kstenerud.cjb.net
 */

 /* Modified by Eke-Eke for Genesis Plus GX:

    - removed unused stuff to reduce memory usage / optimize execution (multiple CPU types support, NMI support, ...)
    - moved stuff to compile statically in a single object file
    - implemented support for global cycle count (shared by 68k & Z80 CPU)
    - added support for interrupt latency (Sesame's Street Counting Cafe, Fatal Rewind)
    - added proper cycle use on reset
    - added cycle accurate timings for MUL/DIV instructions (thanks to Jorge Cwik !) 
    - fixed undocumented flags for DIV instructions (Blood Shot)
    - fixed undocumented behaviors for ABCD/SBCD/NBCD instructions (thanks to flamewing for his test ROM)
    - improved auto-vectored interrupts acknowledge cycle timing accuracy
    - added MAIN-CPU & SUB-CPU support for Mega CD emulation
    
  */

/* ======================================================================== */
/* ================================ INCLUDES ============================== */
/* ======================================================================== */

#include <setjmp.h>
#include "macros.h"
#ifdef HOOK_CPU
#include "cpuhook.h"
#endif

/* ======================================================================== */
/* ==================== ARCHITECTURE-DEPENDANT DEFINES ==================== */
/* ======================================================================== */

/* Check for > 32bit sizes */
#if UINT_MAX > 0xffffffff
  #define M68K_INT_GT_32_BIT  1
#else
  #define M68K_INT_GT_32_BIT  0
#endif

/* Data types used in this emulation core */
#undef sint8
#undef sint16
#undef sint32
#undef sint64
#undef uint8
#undef uint16
#undef uint32
#undef uint64
#undef sint
#undef uint

#define sint8  signed   char      /* ASG: changed from char to signed char */
#define sint16 signed   short
#define sint32 signed   int      /* AWJ: changed from long to int */
#define uint8  unsigned char
#define uint16 unsigned short
#define uint32 unsigned int      /* AWJ: changed from long to int */

/* signed and unsigned int must be at least 32 bits wide */
#define sint   signed   int
// #define uint   unsigned int


#if M68K_USE_64_BIT
#define sint64 signed   long long
#define uint64 unsigned long long
#else
#define sint64 sint32
#define uint64 uint32
#endif /* M68K_USE_64_BIT */



/* Allow for architectures that don't have 8-bit sizes */
/*#if UCHAR_MAX == 0xff*/
  #define MAKE_INT_8(A) (sint8)(A)
/*#else
  #undef  sint8
  #define sint8  signed   int
  #undef  uint8
  #define uint8  unsigned int
  INLINE sint MAKE_INT_8(uint32 value)
  {
    return (value & 0x80) ? value | ~0xff : value & 0xff;
  }*/
/*#endif *//* UCHAR_MAX == 0xff */


/* Allow for architectures that don't have 16-bit sizes */
/*#if USHRT_MAX == 0xffff*/
  #define MAKE_INT_16(A) (sint16)(A)
/*#else
  #undef  sint16
  #define sint16 signed   int
  #undef  uint16
  #define uint16 unsigned int
  INLINE sint MAKE_INT_16(uint32 value)
  {
    return (value & 0x8000) ? value | ~0xffff : value & 0xffff;
  }*/
/*#endif *//* USHRT_MAX == 0xffff */


/* Allow for architectures that don't have 32-bit sizes */
/*#if UINT_MAX == 0xffffffff*/
  #define MAKE_INT_32(A) (sint32)(A)
/*#else
  #undef  sint32
  #define sint32  signed   int
  #undef  uint32
  #define uint32  unsigned int
  INLINE sint MAKE_INT_32(uint32 value)
  {
    return (value & 0x80000000) ? value | ~0xffffffff : value & 0xffffffff;
  }*/
/*#endif *//* UINT_MAX == 0xffffffff */



/* ======================================================================== */
/* ============================ GENERAL DEFINES =========================== */

#ifndef FALSE
#define FALSE 0
#define TRUE 1
#endif

/* ======================================================================== */
/* ==================== NEO GEO MEMORY ACCESS MACROS ====================== */
/* ======================================================================== */

/*
 * Neo Geo 68K memory map:
 *   0x000000-0x0FFFFF : CPU ROM bank 0 (up to 1MB)
 *   0x100000-0x10FFFF : Work RAM (64KB)
 *   0x200000-0x2FFFFF : Banked CPU ROM
 *   0x300000-0x3FFFFF : I/O (controllers, video, settings)
 *   0x400000-0x401FFF : Palette RAM
 *   0x800000-0x800FFF : Memory card
 *   0xC00000-0xCFFFFF : BIOS ROM
 *   0xD00000-0xDFFFFF : SRAM
 *
 * ROM and RAM are byte-swapped at load time (swap_memory), so
 * 16-bit reads can use direct pointer casts on little-endian.
 * 8-bit reads use XOR^1 to compensate for the byte swap.
 */

extern unsigned char *neogeo_cpu_rom;    /* memory.rom.cpu_m68k.p (swapped) */
extern unsigned char *neogeo_bios_rom;   /* memory.rom.bios_m68k.p (swapped) */
extern unsigned char *neogeo_work_ram;   /* memory.ram (swapped) */
extern unsigned int   neogeo_cpu_rom_size;
extern unsigned int   neogeo_bankaddress;

/* --- ROM (bank 0: 0x000000-0x0FFFFF) --- */
#define FETCH8ROM(A)  (neogeo_cpu_rom[((A) & 0xFFFFF) ^ 1])
#define FETCH16ROM(A) (*(unsigned short *)&neogeo_cpu_rom[(A) & 0xFFFFF])
#define FETCH32ROM(A) ((*(unsigned int *)&neogeo_cpu_rom[(A) & 0xFFFFF] << 16) | \
                       (*(unsigned int *)&neogeo_cpu_rom[(A) & 0xFFFFF] >> 16))

/* --- Work RAM (0x100000-0x10FFFF) --- */
#define FETCH8RAM(A)  (neogeo_work_ram[((A) & 0xFFFF) ^ 1])
#define FETCH16RAM(A) (*(unsigned short *)&neogeo_work_ram[(A) & 0xFFFF])
#define FETCH32RAM(A) ((*(unsigned int *)&neogeo_work_ram[(A) & 0xFFFF] << 16) | \
                       (*(unsigned int *)&neogeo_work_ram[(A) & 0xFFFF] >> 16))

#define WRITE8RAM(A, V)  (neogeo_work_ram[((A) & 0xFFFF) ^ 1] = (V))
#define WRITE16RAM(A, V) (*(unsigned short *)&neogeo_work_ram[(A) & 0xFFFF] = (V))
#define WRITE32RAM(A, V) (*(unsigned int *)&neogeo_work_ram[(A) & 0xFFFF] = (((V) << 16) | ((V) >> 16)))

/* --- BIOS (0xC00000-0xC1FFFF) --- */
#define FETCH8BIOS(A)  (neogeo_bios_rom[((A) & 0x1FFFF) ^ 1])
#define FETCH16BIOS(A) (*(unsigned short *)&neogeo_bios_rom[(A) & 0x1FFFF])
#define FETCH32BIOS(A) ((*(unsigned int *)&neogeo_bios_rom[(A) & 0x1FFFF] << 16) | \
                        (*(unsigned int *)&neogeo_bios_rom[(A) & 0x1FFFF] >> 16))

/* --- Banked ROM (0x200000-0x2FFFFF) --- */
#define FETCH8BANK(A)  (neogeo_cpu_rom[(((A) & 0xFFFFF) + neogeo_bankaddress) ^ 1])
#define FETCH16BANK(A) (*(unsigned short *)&neogeo_cpu_rom[((A) & 0xFFFFF) + neogeo_bankaddress])
#define FETCH32BANK(A) ((*(unsigned int *)&neogeo_cpu_rom[((A) & 0xFFFFF) + neogeo_bankaddress] << 16) | \
                        (*(unsigned int *)&neogeo_cpu_rom[((A) & 0xFFFFF) + neogeo_bankaddress] >> 16))

/* --- Immediate fetch (opcode reads) --- */
/* PC can be in ROM bank0, banked ROM, BIOS, or RAM */
static inline unsigned int neogeo_read_imm_16(unsigned int A) {
    unsigned int region = (A >> 20) & 0xF;
    if (region == 0x0) return FETCH16ROM(A);
    if (region == 0x1) return FETCH16RAM(A);
    if (region == 0x2) return FETCH16BANK(A);
    if (region == 0xC) return FETCH16BIOS(A);
    return 0xFFFF;
}
static inline unsigned int neogeo_read_imm_32(unsigned int A) {
    unsigned int region = (A >> 20) & 0xF;
    if (region == 0x0) return FETCH32ROM(A);
    if (region == 0x1) return FETCH32RAM(A);
    if (region == 0x2) return FETCH32BANK(A);
    if (region == 0xC) return FETCH32BIOS(A);
    return 0xFFFFFFFF;
}

#define m68k_read_immediate_16(A) neogeo_read_imm_16(A)
#define m68k_read_immediate_32(A) neogeo_read_imm_32(A)

#define m68k_read_pcrelative_8(A)  FETCH8ROM(A)
#define m68k_read_pcrelative_16(A) neogeo_read_imm_16(A)
#define m68k_read_pcrelative_32(A) neogeo_read_imm_32(A)

/* Read from anywhere */
unsigned int  m68k_read_memory_8(unsigned int address);
unsigned int  m68k_read_memory_16(unsigned int address);
unsigned int  m68k_read_memory_32(unsigned int address);

/* Write to anywhere */
void m68k_write_memory_8(unsigned int address, unsigned int value);
void m68k_write_memory_16(unsigned int address, unsigned int value);
void m68k_write_memory_32(unsigned int address, unsigned int value);

/*** BZHXX ***/
/* ======================================================================== */

/* There are 7 levels of interrupt to the 68K.
 * A transition from < 7 to 7 will cause a non-maskable interrupt (NMI).
 */
#define M68K_IRQ_NONE 0
#define M68K_IRQ_1    1
#define M68K_IRQ_2    2
#define M68K_IRQ_3    3
#define M68K_IRQ_4    4
#define M68K_IRQ_5    5
#define M68K_IRQ_6    6
#define M68K_IRQ_7    7


/* Special interrupt acknowledge values.
 * Use these as special returns from the interrupt acknowledge callback
 * (specified later in this header).
 */

/* Causes an interrupt autovector (0x18 + interrupt level) to be taken.
 * This happens in a real 68K if VPA or AVEC is asserted during an interrupt
 * acknowledge cycle instead of DTACK.
 */
#define M68K_INT_ACK_AUTOVECTOR    0xffffffff

/* Causes the spurious interrupt vector (0x18) to be taken
 * This happens in a real 68K if BERR is asserted during the interrupt
 * acknowledge cycle (i.e. no devices responded to the acknowledge).
 */
#define M68K_INT_ACK_SPURIOUS      0xfffffffe


/* Registers used by m68k_get_reg() and m68k_set_reg() */
typedef enum
{
  /* Real registers */
  M68K_REG_D0,    /* Data registers */
  M68K_REG_D1,
  M68K_REG_D2,
  M68K_REG_D3,
  M68K_REG_D4,
  M68K_REG_D5,
  M68K_REG_D6,
  M68K_REG_D7,
  M68K_REG_A0,    /* Address registers */
  M68K_REG_A1,
  M68K_REG_A2,
  M68K_REG_A3,
  M68K_REG_A4,
  M68K_REG_A5,
  M68K_REG_A6,
  M68K_REG_A7,
  M68K_REG_PC,    /* Program Counter */
  M68K_REG_SR,    /* Status Register */
  M68K_REG_SP,    /* The current Stack Pointer (located in A7) */
  M68K_REG_USP,   /* User Stack Pointer */
  M68K_REG_ISP,   /* Interrupt Stack Pointer */

#if M68K_EMULATE_PREFETCH
  /* Assumed registers */
  /* These are cheat registers which emulate the 1-longword prefetch
   * present in the 68000 and 68010.
   */
  M68K_REG_PREF_ADDR,  /* Last prefetch address */
  M68K_REG_PREF_DATA,  /* Last prefetch data */
#endif

  /* Convenience registers */
  M68K_REG_IR    /* Instruction register */
} m68k_register_t;


/* 68k memory map structure */
typedef struct 
{
  unsigned char *base;                             /* memory-based access (ROM, RAM) */
  unsigned int (*read8)(unsigned int address);               /* I/O byte read access */
  unsigned int (*read16)(unsigned int address);              /* I/O word read access */
  void (*write8)(unsigned int address, unsigned int data);  /* I/O byte write access */
  void (*write16)(unsigned int address, unsigned int data); /* I/O word write access */
} cpu_memory_map;

/* 68k idle loop detection */
typedef struct
{
  uint32 pc;
  uint32 cycle;
  uint32 detected;
} cpu_idle_t;

typedef struct
{
  cpu_memory_map memory_map[256]; /* memory mapping */

  cpu_idle_t poll;      /* polling detection */

  uint32 cycles;          /* current master cycle count */
  uint32 cycle_end;       /* aimed master cycle count for current execution frame */

  uint32 dar[16];         /* Data and Address Registers */
  uint32 pc;              /* Program Counter */
  uint32 sp[5];           /* User and Interrupt Stack Pointers */
  uint32 ir;              /* Instruction Register */
  uint32 t1_flag;         /* Trace 1 */
  uint32 s_flag;          /* Supervisor */
  uint32 x_flag;          /* Extend */
  uint32 n_flag;          /* Negative */
  uint32 not_z_flag;      /* Zero, inverted for speedups */
  uint32 v_flag;          /* Overflow */
  uint32 c_flag;          /* Carry */
  uint32 int_mask;        /* I0-I2 */
  uint32 int_level;       /* State of interrupt pins IPL0-IPL2 -- ASG: changed from ints_pending */
  uint32 stopped;         /* Stopped state */

  uint32 pref_addr;       /* Last prefetch address */
  uint32 pref_data;       /* Data in the prefetch queue */

  uint32 instr_mode;      /* Stores whether we are in instruction mode or group 0/1 exception mode */
  uint32 run_mode;        /* Stores whether we are processing a reset, bus error, address error, or something else */
  uint32 aerr_enabled;    /* Enables/deisables address error checks at runtime */
  jmp_buf aerr_trap;    /* Address error jump */
  uint32 aerr_address;    /* Address error location */
  uint32 aerr_write_mode; /* Address error write mode */
  uint32 aerr_fc;         /* Address error FC code */

  uint32 tracing;         /* Tracing enable flag */

  uint32 address_space;   /* Current FC code */

#ifdef M68K_OVERCLOCK_SHIFT
  int cycle_ratio;
#endif

  /* Callbacks to host */
  int  (*int_ack_callback)(int int_line);           /* Interrupt Acknowledge */
  void (*reset_instr_callback)(void);               /* Called when a RESET instruction is encountered */
  int  (*tas_instr_callback)(void);                 /* Called when a TAS instruction is encountered, allows / disallows writeback */
  void (*set_fc_callback)(unsigned int new_fc);     /* Called when the CPU function code changes */
} m68ki_cpu_core;

/* CPU cores */
extern m68ki_cpu_core *m68k;

/* ======================================================================== */
/* ============================== CALLBACKS =============================== */
/* ======================================================================== */

/* These functions allow you to set callbacks to the host when specific events
 * occur.  Note that you must enable the corresponding value in m68kconf.h
 * in order for these to do anything useful.
 * Note: I have defined default callbacks which are used if you have enabled
 * the corresponding #define in m68kconf.h but either haven't assigned a
 * callback or have assigned a callback of NULL.
 */

#if M68K_EMULATE_INT_ACK == OPT_ON
/* Set the callback for an interrupt acknowledge.
 * You must enable M68K_EMULATE_INT_ACK in m68kconf.h.
 * The CPU will call the callback with the interrupt level being acknowledged.
 * The host program must return either a vector from 0x02-0xff, or one of the
 * special interrupt acknowledge values specified earlier in this header.
 * If this is not implemented, the CPU will always assume an autovectored
 * interrupt, and will automatically clear the interrupt request when it
 * services the interrupt.
 * Default behavior: return M68K_INT_ACK_AUTOVECTOR.
 */
void m68k_set_int_ack_callback(int  (*callback)(int int_level));
#endif

#if M68K_EMULATE_RESET == OPT_ON
/* Set the callback for the RESET instruction.
 * You must enable M68K_EMULATE_RESET in m68kconf.h.
 * The CPU calls this callback every time it encounters a RESET instruction.
 * Default behavior: do nothing.
 */
void m68k_set_reset_instr_callback(void  (*callback)(void));
#endif

#if M68K_TAS_HAS_CALLBACK == OPT_ON
/* Set the callback for the TAS instruction.
 * You must enable M68K_TAS_HAS_CALLBACK in m68kconf.h.
 * The CPU calls this callback every time it encounters a TAS instruction.
 * Default behavior: return 1, allow writeback.
 */
void m68k_set_tas_instr_callback(int  (*callback)(void));
#endif

#if M68K_EMULATE_FC == OPT_ON
/* Set the callback for CPU function code changes.
 * You must enable M68K_EMULATE_FC in m68kconf.h.
 * The CPU calls this callback with the function code before every memory
 * access to set the CPU's function code according to what kind of memory
 * access it is (supervisor/user, program/data and such).
 * Default behavior: do nothing.
 */
void m68k_set_fc_callback(void  (*callback)(unsigned int new_fc));
#endif


/* ======================================================================== */
/* ====================== FUNCTIONS TO ACCESS THE CPU ===================== */
/* ======================================================================== */

/* Do whatever initialisations the core requires.  Should be called
 * at least once at init time.
 */
extern void m68k_init(void);

/* Pulse the RESET pin on the CPU.
 * You *MUST* reset the CPU at least once to initialize the emulation
 */
extern void m68k_pulse_reset(void);

/* Run until given cycle count is reached */
extern void m68k_run(unsigned int cycles);

/* Get current instruction execution time */
extern int m68k_cycles(void);

/* Number of cycles run so far from start of frame */
extern int m68k_cycles_master(void);

/* Number of cycles run so far from run call */
extern int m68k_cycles_run(void);

/* Set the IPL0-IPL2 pins on the CPU (IRQ).
 * A transition from < 7 to 7 will cause a non-maskable interrupt (NMI).
 * Setting IRQ to 0 will clear an interrupt request.
 */
extern void m68k_set_irq(unsigned int int_level);
extern void m68k_set_irq_delay(unsigned int int_level);
extern void m68k_update_irq(unsigned int mask);

/* Halt the CPU as if you pulsed the HALT pin. */
extern void m68k_pulse_halt(void);
extern void m68k_clear_halt(void);

/* Peek at the internals of a CPU context.  This can either be a context
 * retrieved using m68k_get_context() or the currently running context.
 * If context is NULL, the currently running CPU context will be used.
 */
extern unsigned int m68k_get_reg(m68k_register_t reg);

/* Poke values into the internals of the currently running CPU context */
extern void m68k_set_reg(m68k_register_t reg, unsigned int value);

/* Load/Save state of CPU */
extern void gwenesis_m68k_save_state();
extern void gwenesis_m68k_load_state();

/* ======================================================================== */
/* ============================== END OF FILE ============================= */
/* ======================================================================== */

#endif /* M68K__HEADER */
