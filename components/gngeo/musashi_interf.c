/*
 * musashi_interf.c — Musashi 68K adapter for GnGeo (Neo Geo emulator)
 *
 * Implements the cpu_68k_* interface declared in memory.h using
 * the Musashi 68000 core instead of generator68k.
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "musashi/m68k.h"
#include "memory.h"
#include "emu.h"
#include "state.h"
#include "conf.h"

/* ======================================================================== */
/* ====================== GLOBALS FOR MUSASHI MACROS ====================== */
/* ======================================================================== */

/* These are referenced by the FETCH/WRITE macros in m68k.h */
unsigned char *neogeo_cpu_rom;
unsigned char *neogeo_bios_rom;
unsigned char *neogeo_work_ram;
unsigned int   neogeo_cpu_rom_size;
unsigned int   neogeo_bankaddress;

/* CPU state — allocated in PSRAM */
#ifdef ESP32_PLATFORM
#include "esp_heap_caps.h"
static m68ki_cpu_core *musashi_cpu_core;
#else
static m68ki_cpu_core musashi_cpu_storage;
static m68ki_cpu_core *musashi_cpu_core = &musashi_cpu_storage;
#endif

/* Track cycle count at frame start so cpu_68k_getcycle() returns per-frame cycles */
static unsigned int frame_start_cycles;

/* Musashi cycle table is scaled by MUL=7 (from m68kconf.h).
 * External callers use real 68K cycles, so we scale at the boundary. */
#define CYCLE_SCALE 2

/* ======================================================================== */
/* ======================== BYTE SWAP HELPER ============================== */
/* ======================================================================== */

static void swap_memory(unsigned char *mem, unsigned int length)
{
    for (unsigned int i = 0; i < length; i += 2) {
        unsigned char tmp = mem[i];
        mem[i] = mem[i + 1];
        mem[i + 1] = tmp;
    }
}

/* ======================================================================== */
/* =================== MEMORY READ/WRITE CALLBACKS ======================== */
/* ======================================================================== */

/*
 * These are called by Musashi for addresses NOT handled by the
 * inline FETCH/WRITE macros (i.e., I/O regions).
 * We dispatch to the existing GnGeo mem68k handler functions.
 *
 * Neo Geo 68K memory map for I/O:
 *   0x300000         : Controller 1
 *   0x320000         : Coin/Z80
 *   0x340000         : Controller 2
 *   0x380000         : Controller 3 / PD4990
 *   0x3A0000         : Settings
 *   0x3C0000         : Video
 *   0x400000-0x401FFF: Palette
 *   0x800000-0x800FFF: Memory card
 *   0xD00000-0xDFFFFF: SRAM
 */

static int io_debug_count = 0;
static int sram_rd_count = 0, sram_wr_count = 0;
#define IO_DEBUG_MAX 0

/* CTL3 ($380000) read counters — per frame */
static int ctl3_r8_count = 0, ctl3_r16_count = 0;
static uint8_t ctl3_last_r8 = 0xFF;
static uint16_t ctl3_last_r16 = 0xFFFF;

unsigned int m68k_read_memory_8(unsigned int address)
{
    unsigned int page = (address >> 16) & 0xFF;

    switch (page) {
    case 0x00: case 0x01: case 0x02: case 0x03:
    case 0x04: case 0x05: case 0x06: case 0x07:
    case 0x08: case 0x09: case 0x0A: case 0x0B:
    case 0x0C: case 0x0D: case 0x0E: case 0x0F:
        return mem68k_fetch_cpu_byte(address);
    case 0x10:
        return mem68k_fetch_ram_byte(address);
    case 0x30: {
        unsigned int v = mem68k_fetch_ctl1_byte(address);
        if (io_debug_count < IO_DEBUG_MAX) { io_debug_count++; printf("IO R8 %08x=%02x\n", address, v); }
        return v;
    }
    case 0x32: {
        unsigned int v = mem68k_fetch_coin_byte(address);
        if (io_debug_count < IO_DEBUG_MAX) { io_debug_count++; printf("IO R8 %08x=%02x\n", address, v); }
        return v;
    }
    case 0x34: return mem68k_fetch_ctl2_byte(address);
    case 0x38: {
        unsigned int v = mem68k_fetch_ctl3_byte(address);
        if ((address & 0xFFFF) <= 1) { ctl3_r8_count++; ctl3_last_r8 = v; }
        if (io_debug_count < IO_DEBUG_MAX) { io_debug_count++; printf("IO R8 %08x=%02x\n", address, v); }
        return v;
    }
    case 0x3C: return mem68k_fetch_video_byte(address);
    case 0x40:
        return mem68k_fetch_pal_byte(address);
    case 0x80:
        return mem68k_fetch_memcrd_byte(address);
    case 0xD0: case 0xD1: case 0xD2: case 0xD3:
    case 0xD4: case 0xD5: case 0xD6: case 0xD7:
    case 0xD8: case 0xD9: case 0xDA: case 0xDB:
    case 0xDC: case 0xDD: case 0xDE: case 0xDF:
        sram_rd_count++;
        return mem68k_fetch_sram_byte(address);
    case 0x20: case 0x21: case 0x22: case 0x23:
    case 0x24: case 0x25: case 0x26: case 0x27:
    case 0x28: case 0x29: case 0x2A: case 0x2B:
    case 0x2C: case 0x2D: case 0x2E: case 0x2F:
        return mem68k_fetch_bk_normal_byte(address);
    case 0xC0: case 0xC1:
        return mem68k_fetch_bios_byte(address);
    default:
        return 0xFF;
    }
}

unsigned int m68k_read_memory_16(unsigned int address)
{
    unsigned int page = (address >> 16) & 0xFF;

    switch (page) {
    case 0x00: case 0x01: case 0x02: case 0x03:
    case 0x04: case 0x05: case 0x06: case 0x07:
    case 0x08: case 0x09: case 0x0A: case 0x0B:
    case 0x0C: case 0x0D: case 0x0E: case 0x0F:
        return mem68k_fetch_cpu_word(address);
    case 0x10:
        return mem68k_fetch_ram_word(address);
    case 0x20: case 0x21: case 0x22: case 0x23:
    case 0x24: case 0x25: case 0x26: case 0x27:
    case 0x28: case 0x29: case 0x2A: case 0x2B:
    case 0x2C: case 0x2D: case 0x2E: case 0x2F:
        return mem68k_fetch_bk_normal_word(address);
    case 0x30: {
        unsigned int v = mem68k_fetch_ctl1_word(address);
        if (io_debug_count < IO_DEBUG_MAX) { io_debug_count++; printf("IO R16 %08x=%04x\n", address, v); }
        return v;
    }
    case 0x32: return mem68k_fetch_coin_word(address);
    case 0x34: return mem68k_fetch_ctl2_word(address);
    case 0x38: {
        unsigned int v = mem68k_fetch_ctl3_word(address);
        if ((address & 0xFFFF) <= 1) { ctl3_r16_count++; ctl3_last_r16 = v; }
        return v;
    }
    case 0x3C: {
        unsigned int v = mem68k_fetch_video_word(address);
        if ((address & 0xF) != 0x2 && io_debug_count < IO_DEBUG_MAX) { io_debug_count++; printf("IO R16 %08x=%04x\n", address, v); }
        return v;
    }
    case 0x40:
        return mem68k_fetch_pal_word(address);
    case 0x80:
        return mem68k_fetch_memcrd_word(address);
    case 0xD0: case 0xD1: case 0xD2: case 0xD3:
    case 0xD4: case 0xD5: case 0xD6: case 0xD7:
    case 0xD8: case 0xD9: case 0xDA: case 0xDB:
    case 0xDC: case 0xDD: case 0xDE: case 0xDF:
        return mem68k_fetch_sram_word(address);
    case 0xC0: case 0xC1:
        return mem68k_fetch_bios_word(address);
    default:
        return 0xFFFF;
    }
}

unsigned int m68k_read_memory_32(unsigned int address)
{
    return (m68k_read_memory_16(address) << 16) | m68k_read_memory_16(address + 2);
}

void m68k_write_memory_8(unsigned int address, unsigned int value)
{
    unsigned int page = (address >> 16) & 0xFF;

    switch (page) {
    case 0x10:
        mem68k_store_ram_byte(address, value);
        return;
    case 0x20: case 0x21: case 0x22: case 0x23:
    case 0x24: case 0x25: case 0x26: case 0x27:
    case 0x28: case 0x29: case 0x2A: case 0x2B:
    case 0x2C: case 0x2D: case 0x2E: case 0x2F:
        mem68k_store_bk_normal_byte(address, value);
        return;
    case 0x30: mem68k_store_invalid_byte(address, value); return;
    case 0x32: mem68k_store_z80_byte(address, value); return;
    case 0x38: mem68k_store_pd4990_byte(address, value); return;
    case 0x3A:
        if (io_debug_count < IO_DEBUG_MAX) { io_debug_count++; printf("IO W8 %08x=%02x\n", address, value); }
        mem68k_store_setting_byte(address, value); return;
    case 0x3C: mem68k_store_video_byte(address, value); return;
    case 0x40:
        mem68k_store_pal_byte(address, value);
        return;
    case 0x80:
        mem68k_store_memcrd_byte(address, value);
        return;
    case 0xD0: case 0xD1: case 0xD2: case 0xD3:
    case 0xD4: case 0xD5: case 0xD6: case 0xD7:
    case 0xD8: case 0xD9: case 0xDA: case 0xDB:
    case 0xDC: case 0xDD: case 0xDE: case 0xDF:
        mem68k_store_sram_byte(address, value);
        sram_wr_count++;
        return;
    }
}

void m68k_write_memory_16(unsigned int address, unsigned int value)
{
    unsigned int page = (address >> 16) & 0xFF;

    switch (page) {
    case 0x10:
        mem68k_store_ram_word(address, value);
        return;
    case 0x20: case 0x21: case 0x22: case 0x23:
    case 0x24: case 0x25: case 0x26: case 0x27:
    case 0x28: case 0x29: case 0x2A: case 0x2B:
    case 0x2C: case 0x2D: case 0x2E: case 0x2F:
        mem68k_store_bk_normal_word(address, value);
        return;
    case 0x30: mem68k_store_invalid_word(address, value); return;
    case 0x32: mem68k_store_z80_word(address, value); return;
    case 0x38: mem68k_store_pd4990_word(address, value); return;
    case 0x3A:
        if (io_debug_count < IO_DEBUG_MAX) { io_debug_count++; printf("IO W16 %08x=%04x\n", address, value); }
        mem68k_store_setting_word(address, value); return;
    case 0x3C: mem68k_store_video_word(address, value); return;
    case 0x40:
        mem68k_store_pal_word(address, value);
        return;
    case 0x80:
        mem68k_store_memcrd_word(address, value);
        return;
    case 0xD0: case 0xD1: case 0xD2: case 0xD3:
    case 0xD4: case 0xD5: case 0xD6: case 0xD7:
    case 0xD8: case 0xD9: case 0xDA: case 0xDB:
    case 0xDC: case 0xDD: case 0xDE: case 0xDF:
        mem68k_store_sram_word(address, value);
        return;
    }
}

void m68k_write_memory_32(unsigned int address, unsigned int value)
{
    m68k_write_memory_16(address, value >> 16);
    m68k_write_memory_16(address + 2, value & 0xFFFF);
}

/* ======================================================================== */
/* =================== INTERRUPT ACKNOWLEDGE CALLBACK ===================== */
/* ======================================================================== */

int irq_ack_count = 0;

static int neogeo_int_ack(int int_level)
{
    /* Neo Geo uses autovectored interrupts.
     * Clear the interrupt level on acknowledge. */
    irq_ack_count++;
    m68k_set_irq(0);
    return M68K_INT_ACK_AUTOVECTOR;
}

/* ======================================================================== */
/* ========================= CPU INTERFACE (GnGeo) ======================== */
/* ======================================================================== */

void cpu_68k_bankswitch(Uint32 address)
{
    neogeo_bankaddress = address;
    bankaddress = address;
}

void cpu_68k_init(void)
{
    printf("MUSASHI 68K CPU INIT\n");

    /* Allocate CPU core in PSRAM */
#ifdef ESP32_PLATFORM
    musashi_cpu_core = (m68ki_cpu_core *)heap_caps_calloc(1, sizeof(m68ki_cpu_core),
                                                          MALLOC_CAP_SPIRAM);
    if (!musashi_cpu_core) {
        printf("ERROR: Failed to allocate Musashi CPU core!\n");
        return;
    }
#endif

    /* Set the global cpu pointer used by Musashi macros */
    extern m68ki_cpu_core *m68k;
    m68k = musashi_cpu_core;

    /* Byte-swap ROM and BIOS to little-endian 16-bit word format.
     * Musashi uses direct *(uint16_t*) reads, so data must be LE.
     *
     * The GNO/zip loader on ESP32 may already produce LE data.
     * Detect by checking game_vector[0]: if the SSP high byte (0x00 in BE)
     * shows as 0x10 (the second byte of 0x0010xxxx in LE), ROM is already LE.
     * game_vector was saved from ROM[0..0x7F] before BIOS vectors overwrote it.
     */
    if (!CF_BOOL(cf_get_item_by_name("dump"))) {
        /* Check if game ROM is already LE using saved game_vector */
        if (memory.game_vector[0] == 0x10) {
            /* ROM loaded in LE format — don't swap, it's already correct */
            printf("Game ROM already LE (gv[0]=0x%02x), skip swap\n",
                   memory.game_vector[0]);
            /* game_vector is also already LE — leave it */
        } else {
            /* ROM is in original big-endian format — swap to LE */
            printf("Game ROM is BE (gv[0]=0x%02x), swapping\n",
                   memory.game_vector[0]);
            swap_memory(memory.rom.cpu_m68k.p, memory.rom.cpu_m68k.size);
            /* Swap saved game vectors to LE */
            swap_memory(memory.game_vector, 0x80);
        }

        /* Handle BIOS byte ordering */
        if (memory.rom.bios_m68k.p[0] == 0x10) {
            printf("BIOS already LE (byte0=0x%02x)\n", memory.rom.bios_m68k.p[0]);
        } else {
            printf("BIOS is BE (byte0=0x%02x), swapping\n", memory.rom.bios_m68k.p[0]);
            swap_memory(memory.rom.bios_m68k.p, memory.rom.bios_m68k.size);
        }

        /* Ensure ROM[0..0x7F] has LE BIOS vectors (init_game may have copied
         * LE BIOS there already, but re-copy to be safe after any ROM swap) */
        memcpy(memory.rom.cpu_m68k.p, memory.rom.bios_m68k.p, 0x80);
    }

    /* Set up pointers for FETCH macros */
    neogeo_cpu_rom   = memory.rom.cpu_m68k.p;
    neogeo_bios_rom  = memory.rom.bios_m68k.p;
    neogeo_work_ram  = memory.ram;
    neogeo_cpu_rom_size = (memory.rom.cpu_m68k.size < 0x100000)
                          ? memory.rom.cpu_m68k.size : 0x100000;
    neogeo_bankaddress = 0;

    /* Debug: compare FETCH vs READ_WORD_ROM for kof98 */
    if (memory.kof98_prot) {
        uint16_t raw = *(uint16_t *)(neogeo_cpu_rom + 0x100);
        uint16_t via_macro = READ_WORD_ROM(neogeo_cpu_rom + 0x100);
        uint16_t fetch = FETCH16ROM(0x100);
        printf("KOF98 post-init: raw=%04x READ_WORD_ROM=%04x FETCH16ROM=%04x bytes=[%02x][%02x]\n",
               raw, via_macro, fetch,
               neogeo_cpu_rom[0x100], neogeo_cpu_rom[0x101]);
    }

    /* Initialize Musashi */
    m68k_init();
    m68k_set_int_ack_callback(neogeo_int_ack);

    /* Debug: verify reset vectors before pulse_reset */
    {
        uint16_t *rom16 = (uint16_t *)neogeo_cpu_rom;
        printf("ROM[0..7] vectors: %04x %04x %04x %04x\n",
               rom16[0], rom16[1], rom16[2], rom16[3]);
        printf("  SSP=%04x%04x  PC=%04x%04x\n",
               rom16[0], rom16[1], rom16[2], rom16[3]);
    }

    /* Reset CPU — reads initial SP and PC from ROM vectors */
    m68k_pulse_reset();
    printf("After reset: PC=%08x SP=%08x\n",
           m68k_get_reg(M68K_REG_PC), m68k_get_reg(M68K_REG_ISP));

    /* Handle bank switching for >1MB ROMs */
    if (memory.rom.cpu_m68k.size > 0x100000) {
        cpu_68k_bankswitch(0);
    }
}

void cpu_68k_reset(void)
{
    m68k_pulse_reset();
}

static int frame_count = 0;

int cpu_68k_run(Uint32 nb_cycle)
{
    frame_count++;

    /* Reset per-frame cycle counter at the start of each frame's run */
    frame_start_cycles = m68k->cycles;
    unsigned int scaled = nb_cycle * CYCLE_SCALE;
    m68k_run(m68k->cycles + scaled);
    int overrun_scaled = (int)(m68k->cycles - frame_start_cycles) - (int)scaled;

    /* Periodic state monitoring (every 60 frames = ~1 sec) */
    if ((frame_count % 60 == 0)) {
        uint8_t *ram = memory.ram;
        uint8_t gmode = ram[0x009A ^ 1];
        uint8_t user_req = ram[0xFDAE ^ 1];
        uint32_t sr = m68k_get_reg(M68K_REG_SR);
        printf("MUSASHI FRAME %d: PC=%08x SR=%04x vec=%d gmode=%d p1=%02x start=%02x coin=%02x ur=%d irqack=%d\n",
               frame_count, m68k_get_reg(M68K_REG_PC), sr,
               memory.current_vector, gmode, memory.intern_p1,
               memory.intern_start, memory.intern_coin, user_req, irq_ack_count);
    }

    /* Detect stuck at BIOS entry — print once when PC first lands there */
    {
        static int stuck_count = 0;
        static int stuck_reported = 0;
        uint32_t pc = m68k_get_reg(M68K_REG_PC);
        if (pc == 0x00C00402) {
            stuck_count++;
            if (stuck_count == 5 && !stuck_reported) {
                stuck_reported = 1;
                uint32_t sr = m68k_get_reg(M68K_REG_SR);
                printf("STUCK at C00402 for %d frames, SR=%04x, irqack=%d, watchdog=%d\n",
                       stuck_count, sr, irq_ack_count, memory.watchdog);
                /* Dump a few BIOS bytes at $C00402 (LE format) */
                uint8_t *bios = memory.rom.bios_m68k.p;
                printf("  BIOS[0402..041F]: ");
                for (int i = 0x402; i < 0x420; i++) printf("%02x ", bios[i]);
                printf("\n");
            }
        } else {
            stuck_count = 0;
            stuck_reported = 0;
        }
    }

    return overrun_scaled > 0 ? (overrun_scaled / CYCLE_SCALE) : 0;
}

void cpu_68k_interrupt(int level)
{
    m68k_set_irq(level);
}

Uint32 cpu_68k_getpc(void)
{
    return m68k_get_reg(M68K_REG_PC);
}

int cpu_68k_getcycle(void)
{
    return (m68k->cycles - frame_start_cycles) / CYCLE_SCALE;
}

int cpu_68k_run_step(void)
{
    unsigned int start = m68k->cycles;
    m68k_run(start + CYCLE_SCALE);
    return (int)(m68k->cycles - start) / CYCLE_SCALE;
}

void cpu_68k_mkstate(gzFile gzf, int mode)
{
    /* Save/restore the entire Musashi CPU state */
    mkstate_data(gzf, musashi_cpu_core, sizeof(m68ki_cpu_core), mode);
}

int cpu_68k_debuger(void (*execstep)(void), void (*dump)(void))
{
    /* Debugger not supported with Musashi */
    return 0;
}
