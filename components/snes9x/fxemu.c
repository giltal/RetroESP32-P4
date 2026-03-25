/* SuperFX/GSU emulation engine — ported from snes9x upstream (fxemu.cpp).
 * Converted to C for this ESP32-P4 fork. */

#include <string.h>
#include "snes9x.h"
#include "memmap.h"
#include "fxinst.h"
#include "fxemu.h"
#include "ppu.h"
#include "cpuexec.h"
#include <esp_log.h>
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
static const char *FXTAG = "SFX";

/* -----------------------------------------------------------------
 * Async GSU task — runs on Core 1, signal via semaphore
 * ----------------------------------------------------------------- */
static SemaphoreHandle_t s_gsu_run_sem  = NULL;  /* signal: start GSU   */
static SemaphoreHandle_t s_gsu_done_sem = NULL;  /* signal: GSU stopped */
static TaskHandle_t      s_gsu_task_handle = NULL;
static volatile bool     s_gsu_running = false;  /* true while GSU is executing */

#include "esp_task_wdt.h"

/* Forward declaration */
static void gsu_task(void *arg);

struct FxInfo_s SuperFX;

/* GSU speed: cycles per scanline. Real GSU-1 = 10.7MHz, GSU-2 = 21.4MHz.
 * At ~262 scanlines/frame, ~60fps, that's ~680 instructions/line for GSU-2.
 * We use a modest value tuned for ESP32-P4 performance. */
#define GSU_CYCLES_PER_LINE  500000

/* ------------------------------------------------------------------
 * Cache management
 * ------------------------------------------------------------------ */
void fx_flushCache(void)
{
    GSU.bCacheActive = false;
    GSU.vCacheFlags = 0;
    GSU.vCacheBaseReg = 0;
}

/* ------------------------------------------------------------------
 * Screen pointer computation
 * ------------------------------------------------------------------ */
void fx_computeScreenPointers(void)
{
    if (!GSU.pvScreenBase || !GSU.pvRam)
        return;

    uint8_t scmr = GSU.pvRegisters[GSU_SCMR];
    int mode = scmr & 0x03;

    /* Bytes per pixel row in a character */
    int ppitch;
    switch (mode) {
        case 0: ppitch = 16; break;  /* 2bpp: 16 bytes/char */
        case 1: ppitch = 32; break;  /* 4bpp: 32 bytes/char */
        case 2: ppitch = 32; break;  /* 4bpp (mode 2 same as 1) */
        default: ppitch = 64; break; /* 8bpp: 64 bytes/char */
    }

    /* Screen height */
    int h = (scmr & 0x04) ? 256 : GSU.vScreenRealHeight;
    if (GSU.vPlotOptionReg & 0x10) h = 256;
    GSU.vScreenHeight = h;
    GSU.vMode = mode;

    /* Tiles per row (screen width / 8 pixels) */
    int tiles_per_row;
    if (scmr & 0x20)      tiles_per_row = 32;  /* 256 pixels wide */
    else if (scmr & 0x04)  tiles_per_row = 16;  /* 128 pixels wide */
    else                    tiles_per_row = 32;  /* default */

    /* X offset table: column * bytes_per_tile */
    for (int i = 0; i < 32; i++)
        GSU.x[i] = i * ppitch;

    /* Screen row pointers (each row = tile-row of 8 scanlines) */
    for (int i = 0; i < 32; i++)
        GSU.apvScreen[i] = GSU.pvScreenBase + (i * tiles_per_row * ppitch);

    /* Update plot function pointers */
    GSU.pfPlot = fx_PlotTable[mode];
    GSU.pfRpix = fx_PlotTable[mode + 5];
}

/* ------------------------------------------------------------------
 * S9xInitSuperFX — called once at startup
 * ------------------------------------------------------------------ */
void S9xInitSuperFX(void)
{
    memset(&GSU, 0, sizeof(FxRegs_s));
    memset(&SuperFX, 0, sizeof(struct FxInfo_s));
}

/* ------------------------------------------------------------------
 * S9xResetSuperFX — called on each ROM load / reset
 * ------------------------------------------------------------------ */
void S9xResetSuperFX(void)
{
    /* Link GSU registers via FxInfo */
    SuperFX.pvRegisters = &Memory.FillRAM[0x3000];
    /* pvRam and nRamBanks must be set by caller BEFORE calling this */
    SuperFX.pvRom = Memory.ROM;
    SuperFX.nRomBanks = Memory.CalculatedSize >> 15;  /* 32KB banks */
    SuperFX.speedPerLine = GSU_CYCLES_PER_LINE;
    SuperFX.oneLineDone = false;

    /* Clear GSU register space in FillRAM and set initial values */
    memset(&Memory.FillRAM[0x3000], 0, 0x400);
    Memory.FillRAM[0x303B] = 0x04;  /* VCR = SuperFX2 version */

    /* Copy into GSU internal state */
    GSU.pvRegisters = SuperFX.pvRegisters;
    GSU.pvRom = SuperFX.pvRom;
    GSU.pvRam = SuperFX.pvRam;
    GSU.nRomBanks = SuperFX.nRomBanks;
    GSU.nRamBanks = SuperFX.nRamBanks;

    /* Set up ROM bank table for GSU LoROM view.
     * In a LoROM SuperFX cartridge, the GSU sees ROM at $8000-$FFFF
     * of each bank. We create a deinterleaved copy in PSRAM where each
     * 32KB chunk is placed at offset $8000 in a 64KB block.
     * $0000-$7FFF mirrors the SAME 32KB chunk. */
    {
        uint32_t romSz = Memory.CalculatedSize;
        uint32_t nBanks = romSz >> 15;  /* number of 32KB chunks */
        uint32_t deintSize = nBanks * 0x10000;
        uint8_t *deintBase = (uint8_t *)heap_caps_calloc(1, deintSize, MALLOC_CAP_SPIRAM);
        if (deintBase) {
            for (uint32_t b = 0; b < nBanks; b++) {
                /* Place ROM data at $8000-$FFFF */
                memcpy(deintBase + b * 0x10000 + 0x8000,
                       Memory.ROM + b * 0x8000, 0x8000);
                /* Mirror the same 32KB at $0000-$7FFF */
                memcpy(deintBase + b * 0x10000,
                       Memory.ROM + b * 0x8000, 0x8000);
            }
            for (int i = 0; i < 256; i++) {
                uint32_t bankIdx = (uint32_t)i % nBanks;
                GSU.apvRomBank[i] = deintBase + bankIdx * 0x10000;
            }
            ESP_LOGI(FXTAG, "GSU ROM deint: %lu banks, %lu bytes",
                (unsigned long)nBanks, (unsigned long)deintSize);
        } else {
            ESP_LOGE(FXTAG, "Failed to alloc GSU ROM (%lu bytes)!",
                (unsigned long)deintSize);
            for (int i = 0; i < 256; i++) {
                uint32_t offset = ((uint32_t)i) * 0x8000;
                GSU.apvRomBank[i] = Memory.ROM + (offset % romSz) - 0x8000;
            }
        }
    }

    /* Set up RAM bank table */
    for (int i = 0; i < FX_RAM_BANKS; i++) {
        GSU.apvRamBank[i] = GSU.pvRam + (i << 16);  /* 64KB per bank */
    }

    /* Default banks */
    GSU.pvRomBank = GSU.apvRomBank[0];
    GSU.pvRamBank = GSU.apvRamBank[0];
    GSU.pvPrgBank = GSU.apvRomBank[0];

    /* Reset registers */
    for (int i = 0; i < 16; i++)
        GSU.avReg[i] = 0;
    GSU.pvSreg = GSU.pvDreg = &GSU.avReg[0];
    GSU.vStatusReg = 0;
    GSU.vPrgBankReg = 0;
    GSU.vRomBankReg = 0;
    GSU.vRamBankReg = 0;
    GSU.vCarry = 0;
    GSU.vZero = 0;
    GSU.vSign = 0;
    GSU.vOverflow = 0;
    GSU.vColorReg = 0;
    GSU.vPlotOptionReg = 0;

    /* Screen */
    GSU.vScreenRealHeight = 256;
    GSU.vScreenHeight = 256;
    GSU.pvScreenBase = GSU.pvRam;
    GSU.vSCBRDirty = true;

    /* Cache */
    fx_flushCache();

    /* Compute initial screen pointers */
    fx_computeScreenPointers();

    /* Create async GSU task if not yet created */
    if (!s_gsu_run_sem) {
        s_gsu_run_sem  = xSemaphoreCreateBinary();
        s_gsu_done_sem = xSemaphoreCreateBinary();
        xSemaphoreGive(s_gsu_done_sem);  /* Start in "done" state */
        xTaskCreatePinnedToCore(gsu_task, "gsu_task",
                                4096, NULL, 10, &s_gsu_task_handle, 1);
        ESP_LOGI(FXTAG, "GSU async task created on Core 1, priority 10");
    }
}

/* ------------------------------------------------------------------
 * fx_readRegisterSpace — sync GSU state from register space ($3000)
 * Called before GSU execution starts.
 * ------------------------------------------------------------------ */
static void fx_readRegisterSpace(void)
{
    uint8_t *r = GSU.pvRegisters;

    /* Read R0-R15 from register space */
    for (int i = 0; i < 16; i++) {
        GSU.avReg[i] = (uint32_t)r[i * 2] | ((uint32_t)r[i * 2 + 1] << 8);
    }

    /* SFR (Status/Flag Register) at $3030-$3031 */
    GSU.vStatusReg = (uint32_t)r[GSU_SFR] | ((uint32_t)r[GSU_SFR + 1] << 8);

    /* PBR (Program Bank Register) at $3034 */
    GSU.vPrgBankReg = (uint32_t)r[GSU_PBR];
    GSU.pvPrgBank = GSU.apvRomBank[GSU.vPrgBankReg];

    /* ROMBR at $3036 */
    GSU.vRomBankReg = (uint32_t)r[GSU_ROMBR];
    GSU.pvRomBank = GSU.apvRomBank[GSU.vRomBankReg];

    /* RAMBR at $303C */
    GSU.vRamBankReg = (uint32_t)(r[GSU_RAMBR] & (FX_RAM_BANKS - 1));
    GSU.pvRamBank = GSU.apvRamBank[GSU.vRamBankReg];

    /* CBR (Cache Base Register) at $303E */
    GSU.vCacheBaseReg = (uint32_t)r[GSU_CBR] | ((uint32_t)r[GSU_CBR + 1] << 8);

    /* SCBR (Screen Base Register) — compute screen base */
    uint8_t scbr = r[GSU_SCBR];
    if (GSU.vSCBRDirty || GSU.vPrevMode != GSU.vMode || GSU.vPrevScreenHeight != GSU.vScreenHeight) {
        GSU.pvScreenBase = GSU.pvRam + ((uint32_t)scbr << 10);
        GSU.vSCBRDirty = false;
        GSU.vPrevMode = GSU.vMode;
        GSU.vPrevScreenHeight = GSU.vScreenHeight;
        fx_computeScreenPointers();
    }

    /* ROM buffer — current value of ROM at R14 address */
    GSU.vRomBuffer = GSU.pvRomBank[R14 & 0xFFFF];
}

/* ------------------------------------------------------------------
 * fx_writeRegisterSpace — sync register space from GSU state
 * Called after GSU execution completes.
 * ------------------------------------------------------------------ */
static void fx_writeRegisterSpace(void)
{
    uint8_t *r = GSU.pvRegisters;

    /* Write R0-R15 back */
    for (int i = 0; i < 16; i++) {
        r[i * 2] = (uint8_t)GSU.avReg[i];
        r[i * 2 + 1] = (uint8_t)(GSU.avReg[i] >> 8);
    }

    /* SFR */
    r[GSU_SFR] = (uint8_t)GSU.vStatusReg;
    r[GSU_SFR + 1] = (uint8_t)(GSU.vStatusReg >> 8);

    /* PBR */
    r[GSU_PBR] = (uint8_t)GSU.vPrgBankReg;

    /* ROMBR */
    r[GSU_ROMBR] = (uint8_t)GSU.vRomBankReg;

    /* RAMBR */
    r[GSU_RAMBR] = (uint8_t)GSU.vRamBankReg;

    /* CBR */
    r[GSU_CBR] = (uint8_t)GSU.vCacheBaseReg;
    r[GSU_CBR + 1] = (uint8_t)(GSU.vCacheBaseReg >> 8);
}

/* ------------------------------------------------------------------
 * GSU async task — runs on Core 1.
 * Waits for s_gsu_run_sem, runs GSU to completion, signals done.
 * ------------------------------------------------------------------ */
static void gsu_task(void *arg)
{
    (void)arg;
    ESP_LOGI(FXTAG, "GSU task started on Core %d", xPortGetCoreID());
    int64_t t_task_start = 0;  /* set on first actual GSU execution */

    /* Subscribe to task watchdog so we can pet it during long GSU bursts */
    esp_task_wdt_add(NULL);

    for (;;) {
        /* Wait for a "go" signal, petting WDT while idle */
        while (xSemaphoreTake(s_gsu_run_sem, pdMS_TO_TICKS(2000)) != pdTRUE) {
            esp_task_wdt_reset();
        }
        esp_task_wdt_reset();

        s_gsu_running = true;

        /* Read registers from FillRAM into GSU struct */
        fx_readRegisterSpace();

        /* Record start of first real GSU program for histogram auto-trigger */
        if (t_task_start == 0) t_task_start = esp_timer_get_time();

        /* Run GSU to completion in chunks, petting the watchdog between chunks.
         * Hard timeout: if the GSU render hasn't finished within GSU_FRAME_TIMEOUT_US,
         * force GO=0 so the SNES CPU's polling loop exits and the game can display
         * whatever partial frame was rendered (gives slow-but-visible gameplay). */
#define GSU_FRAME_TIMEOUT_US  500000LL  /* 500 ms per frame max */
        uint32_t total_ops = 0;
        int64_t t0 = esp_timer_get_time();
        int64_t t_last_log = t0;
        uint32_t ops_at_last_log = 0;
        bool timed_out = false;
        while (GSU.vStatusReg & FLG_G) {
            uint32_t ran = fx_run(200000);
            total_ops += ran;
            esp_task_wdt_reset();
            int64_t now = esp_timer_get_time();
            /* Log ops/sec every 10 seconds */
            if (now - t_last_log >= 10000000LL) {
                uint32_t delta_ops = total_ops - ops_at_last_log;
                int64_t delta_us = now - t_last_log;
                ESP_LOGI(FXTAG, "GSU rate: %lu ops / %lld ms = %llu kops/sec (total %lu ops)",
                         (unsigned long)delta_ops,
                         (long long)(delta_us / 1000),
                         (uint64_t)delta_ops * 1000ULL / (uint64_t)(delta_us / 1000),
                         (unsigned long)total_ops);
                t_last_log = now;
                ops_at_last_log = total_ops;
            }
            /* Hard timeout — force GO=0 so the SNES CPU unblocks */
            if ((now - t0) >= GSU_FRAME_TIMEOUT_US) {
                GSU.vStatusReg &= ~FLG_G;
                timed_out = true;
                break;
            }
        }
        int64_t elapsed_us = esp_timer_get_time() - t0;
        if (timed_out) {
            ESP_LOGW(FXTAG, "GSU frame TIMEOUT after %lu ops in %lld ms (forced GO=0)",
                     (unsigned long)total_ops, (long long)(elapsed_us / 1000));
        } else {
            ESP_LOGI(FXTAG, "GSU frame done: %lu ops in %lld us = %llu kops/sec",
                     (unsigned long)total_ops, elapsed_us,
                     elapsed_us > 0 ? (uint64_t)total_ops * 1000000ULL / (uint64_t)elapsed_us : 0);
        }

        /* Write results back to FillRAM */
        fx_writeRegisterSpace();

        s_gsu_running = false;

        /* Signal completion */
        xSemaphoreGive(s_gsu_done_sem);
    }
}

/* ------------------------------------------------------------------
 * S9xSuperFXStartAsync — kick off GSU execution asynchronously.
 * Safe to call from Core 0 (SNES CPU context).
 * ------------------------------------------------------------------ */
static void S9xSuperFXStartAsync(void)
{
    if (!Settings.SuperFX) return;
    if (!(Memory.FillRAM[0x3030] & FLG_G)) return;
    if ((Memory.FillRAM[0x303A] & 0x18) == 0) return;
    if (s_gsu_running) return;  /* Already running */

    /* Drain any previous "done" signal so we don't overlap */
    xSemaphoreTake(s_gsu_done_sem, 0);

    /* Signal GSU task to start */
    xSemaphoreGive(s_gsu_run_sem);
}

/* ------------------------------------------------------------------
 * S9xSuperFXExec — check/advance GSU; called from HBlank on Core 0.
 * In async mode, we only signal the task if GO=1 and task is idle.
 * ------------------------------------------------------------------ */
void S9xSuperFXExec(void)
{
    if (!Settings.SuperFX)
        return;

    /* Check if GSU GO flag is set */
    if (!(Memory.FillRAM[0x3030] & FLG_G))
        return;

    /* Also check SCMR RON/RAN bits — GSU needs bus access */
    if ((Memory.FillRAM[0x303A] & 0x18) == 0)
        return;

    /* Async mode: signal the GSU task if not already running */
    S9xSuperFXStartAsync();
}

/* ------------------------------------------------------------------
 * S9xSetSuperFX — write handler for $3000-$303F register space
 * called from S9xSetPPU when Address is in $3000-$32FF range
 * ------------------------------------------------------------------ */
void S9xSetSuperFX(uint8_t byte, uint16_t address)
{
    uint32_t off = address - 0x3000;

    /* Log first N register writes for debugging */
    {
        static int sfx_write_log = 0;
        if (sfx_write_log < 20 && address < 0x3100) {
            ESP_LOGI(FXTAG, "W $%04X=%02X", address, byte);
            sfx_write_log++;
        }
    }

    /* For most addresses, store directly to FillRAM register space.
     * Control registers $3030-$303F are handled specially below. */
    if (off >= 0x40 || off < 0x30) {
        /* Cache ($3100-$32FF), regular registers R0-R15, etc. */
        Memory.FillRAM[address] = byte;
    }

    switch (address) {
        case 0x3030:  /* SFR low byte — only GO bit writable by CPU */
            if ((Memory.FillRAM[0x3030] ^ byte) & FLG_G) {
                Memory.FillRAM[0x3030] = byte;
                if (byte & FLG_G) {
                    /* GO transitions 0→1: start GSU */
                    GSU.vStatusReg |= FLG_G;
                    S9xClearIRQ(GSU_IRQ_SOURCE);
                    S9xSuperFXStartAsync();
                } else {
                    /* GO transitions 1→0: stop GSU, flush cache */
                    GSU.vStatusReg &= ~FLG_G;
                    fx_flushCache();
                }
            } else {
                Memory.FillRAM[0x3030] = byte;
            }
            break;

        case 0x3031:  /* SFR high byte — mostly read-only */
            /* Bit 7 (IRQ) can be cleared by CPU writing 0 */
            if (!(byte & 0x80))
                Memory.FillRAM[0x3031] &= 0x7F;
            break;

        case 0x3033:  /* BRAMR */
            Memory.FillRAM[address] = byte;
            break;

        case 0x3034:  /* PBR */
            Memory.FillRAM[address] = byte;
            GSU.vPrgBankReg = byte & 0x7f;
            GSU.pvPrgBank = GSU.apvRomBank[GSU.vPrgBankReg];
            break;

        case 0x3037:  /* CFGR */
            Memory.FillRAM[address] = byte;
            break;

        case 0x3038:  /* SCBR — screen base register */
            Memory.FillRAM[address] = byte;
            GSU.vSCBRDirty = true;
            break;

        case 0x3039:  /* CLSR — clock speed select */
            Memory.FillRAM[address] = byte;
            break;

        case 0x303A:  /* SCMR — screen mode register */
            Memory.FillRAM[address] = byte;
            GSU.vSCBRDirty = true;
            break;

        case 0x303B:  /* VCR — read-only version register, writes ignored */
            break;

        case 0x303C:  /* RAMBR */
            Memory.FillRAM[address] = byte;
            GSU.vRamBankReg = byte & (FX_RAM_BANKS - 1);
            GSU.pvRamBank = GSU.apvRamBank[GSU.vRamBankReg];
            break;

        case 0x301F:  /* R15 high byte — triggers GSU execution */
            Memory.FillRAM[0x301F] = byte;
            GSU.avReg[15] = (GSU.avReg[15] & 0x00FF) | ((uint32_t)byte << 8);
            /* Set GO flag and run GSU immediately */
            Memory.FillRAM[0x3030] |= FLG_G;
            GSU.vStatusReg |= FLG_G;
            S9xClearIRQ(GSU_IRQ_SOURCE);
            {
                static int r15h_log = 0;
                if (r15h_log < 20) {
                    uint16_t cpu_pc = (uint16_t)(CPU.PCAtOpcodeStart - CPU.PCBase);
                    uint8_t cpu_pb = ICPU.Registers.PB;
                    ESP_LOGI(FXTAG, "R15H GO! R15=%04X CPU_PC=%02X:%04X PBR=%02X SCMR=%02X",
                        (unsigned)GSU.avReg[15], cpu_pb, cpu_pc,
                        Memory.FillRAM[0x3034], Memory.FillRAM[0x303A]);
                    r15h_log++;
                }
            }
            S9xSuperFXStartAsync();
            break;

        default:
            /* Register writes to R0-R15 ($3000-$301E) */
            if (off < 0x20) {
                int reg = off >> 1;
                if (off & 1)
                    GSU.avReg[reg] = (GSU.avReg[reg] & 0x00FF) | ((uint32_t)byte << 8);
                else
                    GSU.avReg[reg] = (GSU.avReg[reg] & 0xFF00) | (uint32_t)byte;
            }
            /* $3020-$302F and $303D-$303F: store in FillRAM */
            else if (off >= 0x30 && off < 0x40) {
                if (address != 0x3030 && address != 0x3031 && address != 0x303B)
                    Memory.FillRAM[address] = byte;
            }
            /* Cache write ($3100-$32FF) */
            if (address >= 0x3100) {
                fx_flushCache();
            }
            break;
    }
}

/* ------------------------------------------------------------------
 * S9xGetSuperFX — read handler for $3000-$303F register space
 * ------------------------------------------------------------------ */
uint8_t S9xGetSuperFX(uint16_t address)
{
    uint8_t val = Memory.FillRAM[address];

    switch (address) {
        case 0x3030:  /* SFR low byte */
            /* Pack flags into SFR for reading */
            val = (uint8_t)GSU.vStatusReg;
            break;

        case 0x3031:  /* SFR high byte — reading clears IRQ */
            val = (uint8_t)(GSU.vStatusReg >> 8);
            Memory.FillRAM[0x3031] = val & 0x7f;  /* Clear IRQ flag after read */
            GSU.vStatusReg &= ~FLG_IRQ;
            S9xClearIRQ(GSU_IRQ_SOURCE);
            break;

        case 0x303B:  /* VCR — version code register */
            val = 0x04;  /* GSU-2 */
            break;

        case 0x303E:  /* CBR low */
            val = (uint8_t)GSU.vCacheBaseReg;
            break;

        case 0x303F:  /* CBR high */
            val = (uint8_t)(GSU.vCacheBaseReg >> 8);
            break;
    }

    return val;
}
