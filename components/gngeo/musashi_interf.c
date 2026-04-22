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

/* MVS auto-boot: flag to enable SRAM[0x5E] read intercept only after BIOS
 * has finished SRAM initialization (avoids disrupting early BIOS init flow) */
static int sram5e_intercept_active = 0;

/* Musashi cycle table is scaled by MUL=7 (from m68kconf.h).
 * External callers use real 68K cycles, so we scale at the boundary. */
#define CYCLE_SCALE 7

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
        {
            unsigned int val = mem68k_fetch_sram_byte(address);
            /* MVS BIOS auto-boot: clear bit 7 of SRAM[0x5E] on read,
             * but only after BIOS has completed SRAM init (~frame 260). */
            if (sram5e_intercept_active && (address & 0xFFFF) == 0x005E)
                val &= 0x7F;
            return val;
        }
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

static int irq_ack_count = 0;

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

    /* Dump game ROM header once at startup - both raw and via FETCH */
    if (frame_count == 5) {
        uint8_t *rom = memory.rom.cpu_m68k.p;
        printf("GAME_ROM raw[100..13F]:\n");
        for (int i = 0; i < 0x40; i += 2) {
            uint16_t w = *(uint16_t*)&rom[0x100 + i];
            printf("%04x ", w);
            if ((i & 0x1E) == 0x1E) printf("\n");
        }
        /* Show what 68K actually sees via FETCH macros (bank ROM 0x200100) */
        printf("FETCH16 bank[200100..20013E]:\n");
        for (int i = 0; i < 0x40; i += 2) {
            uint16_t w = FETCH16BANK(0x200100 + i);
            printf("%04x ", w);
            if ((i & 0x1E) == 0x1E) printf("\n");
        }
        /* Show game vector table at ROM offset 0x00-0x7F */
        printf("game_vector[00..0F]: ");
        for (int i = 0; i < 0x10; i++)
            printf("%02x ", memory.game_vector[i]);
        printf("\n");
        /* Show user routine vectors via FETCH */
        printf("User vectors: startup=%08x eyecatch=%08x demo=%08x title=%08x\n",
               FETCH32BANK(0x200114), FETCH32BANK(0x200118),
               FETCH32BANK(0x20011C), FETCH32BANK(0x200120));
        /* Show VBL vector from game ROM offset 0x64 */
        printf("Game VBL vector (ROM[0x64]): %08x\n",
               (FETCH16BANK(0x200064) << 16) | FETCH16BANK(0x200066));
    }

    /* No auto-boot hacks — BIOS boots naturally with correct ROM byte order */

    /* Reset per-frame cycle counter at the start of each frame's run */
    frame_start_cycles = m68k->cycles;
    unsigned int scaled = nb_cycle * CYCLE_SCALE;
    m68k_run(m68k->cycles + scaled);
    int overrun_scaled = (int)(m68k->cycles - frame_start_cycles) - (int)scaled;

    /* Periodic state monitoring (every 60 frames = 1 sec) */
    if ((frame_count % 60 == 0)) {
        uint8_t *ram = memory.ram;
        uint16_t state = (ram[0xFCD8 + 1] << 8) | ram[0xFCD8];
        /* Game mode at $10009A (68K byte → ram[0x009A ^ 1] = ram[0x009B]) */
        uint8_t gmode = ram[0x009A ^ 1];
        /* BIOS control at $10FD80 (68K byte → ram[0xFD80 ^ 1] = ram[0xFD81]) */
        uint8_t fd80 = ram[0xFD80 ^ 1];
        /* Credit/coin counters - check multiple possible locations */
        uint8_t cred1 = ram[0xD8A8 ^ 1]; /* $10D8A8 common BIOS credit location */
        uint8_t cred2 = ram[0xFE06 ^ 1]; /* $10FE06 */
        uint8_t cred3 = ram[0xFE30 ^ 1]; /* $10FE30 */
        uint8_t fee2  = ram[0xFEE2 ^ 1]; /* A5+$0BE2 (old injection, should be 0 now) */
        /* BIOS handshake variables */
        uint8_t user_req = ram[0xFDAE ^ 1]; /* $10FDAE: BIOS_USER_REQUEST */
        uint8_t user_mod = ram[0xFDAF ^ 1]; /* $10FDAF: BIOS_USER_MODE */
        uint8_t start_fl = ram[0xFDB4 ^ 1]; /* $10FDB4: BIOS_START_FLAG */
        uint8_t plyr_mod = ram[0xFDB6 ^ 1]; /* $10FDB6: BIOS_PLAYER_MOD1 */
        /* SRAM credits */
        uint8_t scred1 = memory.sram[0x34]; /* $D00034: P1 credits */
        uint8_t scred2 = memory.sram[0x35]; /* $D00035: P2 credits */
        uint8_t sfree  = memory.sram[0x42]; /* $D00042: free play */
        printf("FRAME %d: PC=%08x vec=%d gmode=%d fd80=%02x p1=%02x start=%02x coin=%02x irq=%d scr=%02x/%02x fp=%d ur=%d um=%d sf=%02x pm=%d\n",
               frame_count, m68k_get_reg(M68K_REG_PC),
               memory.current_vector, gmode, fd80, memory.intern_p1,
               memory.intern_start, memory.intern_coin, irq_ack_count,
               scred1, scred2, sfree, user_req, user_mod, start_fl, plyr_mod);
        /* Dump BIOS handshake area + SRAM credits */
        printf("  BIOS[FDB0..FDC0]: ");
        for (int bi = 0; bi < 16; bi++) printf("%02x ", ram[0xFDB0 + bi]);
        printf("\n  SRAM[30..50]: ");
        for (int bi = 0x30; bi < 0x50; bi++) printf("%02x ", memory.sram[bi]);
        printf("\n  CTL3: r8=%d r16=%d last8=%02x last16=%04x\n",
               ctl3_r8_count, ctl3_r16_count, ctl3_last_r8, ctl3_last_r16);
        ctl3_r8_count = 0; ctl3_r16_count = 0;
    }

    /* One-shot code dump when game is in main loop */
    {
        static int code_dump_done = 0;
        uint32_t pc = m68k_get_reg(M68K_REG_PC);
        if (!code_dump_done && pc >= 0x3700 && pc < 0x3900 && frame_count > 300) {
            code_dump_done = 1;
            printf("Game main loop active at PC=%08x\n", pc);
            /* Dump game ROM vector table at $000100-$000140 */
            uint8_t *rom = memory.rom.cpu_m68k.p;
            if (rom) {
                printf("ROM vectors $100-$140:\n");
                for (int i = 0x100; i < 0x140; i += 4) {
                    /* ROM is stored little-endian, 68K is big-endian.
                     * Already byte-swapped during load? Check first. */
                    uint32_t val = (rom[i] << 24) | (rom[i+1] << 16) |
                                   (rom[i+2] << 8) | rom[i+3];
                    uint32_t val_le = rom[i] | (rom[i+1] << 8) |
                                      (rom[i+2] << 16) | (rom[i+3] << 24);
                    printf("  $%04x: %02x %02x %02x %02x  (BE=%08x LE=%08x)\n",
                           i, rom[i], rom[i+1], rom[i+2], rom[i+3], val, val_le);
                }
            }
        }
    }

    /* Dump input-related RAM ranges when button is pressed (once per press) */
    {
        static int input_dump_done = 0;
        if (memory.intern_p1 != 0xFF && !input_dump_done) {
            input_dump_done = 1;
            uint8_t *ram = memory.ram;
            uint8_t gmode = ram[0x009A ^ 1];
            printf("INPUT p1=%02x gmode=%d scr=%02x/%02x\n", memory.intern_p1, gmode,
                   memory.sram[0x34], memory.sram[0x35]);
        }
        if (memory.intern_p1 == 0xFF) input_dump_done = 0;
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
