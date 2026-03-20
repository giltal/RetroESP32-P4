# RetroESP32-P4 — Development Log & Session Continuity Guide

> **Last Updated:** March 2026 — **GitHub push** (RetroESP32-P4 repo, .gitignore, merged firmware), **Atari 5200 support** (.a52 extension, 5200 cart mode, direct PPA 480×640 display pipeline, X/Y button fix), **SNES save/load state** (full emulator snapshot to SD card, menu integration), **SNES DKC crash fix** (NO_ZERO_LUT — COLOR_SUB1_2 NULL dereference), SNES (snes9x) integration & optimization (42→50 FPS, dual-core audio offload, direct 2× PPA scaling, DSP tuning), ZX Spectrum full optimization (PPA direct 320×240→480×640 pipeline, Kempston joystick, -O3, 41→50 FPS), launcher native 800×480 UI overhaul (PNG artwork, VGA font, icon fixes), PCE save/load state (v4 format), Atari 800 async audio (52→60 FPS), PCE 60 FPS optimization, ZX Spectrum crash fixes, Atari 7800 exit fix, PPA S→R→M fix, OpenTyrian integration, in-game menus for all emulators, launcher browser fixes.
> **Read this file at the start of every new session to pick up where we left off.**

---

## Table of Contents

1. [Project Overview](#1-project-overview)
2. [Hardware Specifications](#2-hardware-specifications)
3. [Build Environment & Commands](#3-build-environment--commands)
4. [Architecture: OTA Multi-Binary](#4-architecture-ota-multi-binary)
5. [Partition Table & Flash Map](#5-partition-table--flash-map)
6. [Display Pipeline (PPA)](#6-display-pipeline-ppa)
7. [Key Source Files](#7-key-source-files)
8. [Emulator Apps & Launcher](#8-emulator-apps--launcher)
9. [Crash Guard System](#9-crash-guard-system)
10. [Completed Work — All Phases](#10-completed-work--all-phases)
11. [Bugs Fixed This Session](#11-bugs-fixed-this-session)
12. [Known Issues & Notes](#12-known-issues--notes)
13. [SD Card Structure](#13-sd-card-structure)
14. [Device Recovery Procedures](#14-device-recovery-procedures)
15. [Future Work / TODO](#15-future-work--todo)
16. [Emulator Comparison Table](#16-emulator-comparison-table)

---

## 1. Project Overview

**Goal:** Port the RetroESP32 multi-emulator platform (originally ESP32-S3/Odroid Go) to the ESP32-P4 Guiton 4.3" development board, with 7 working console emulators.

**Source Projects:**
- `C:\ESPIDFprojects\RetroESP32-master` — Original multi-emulator platform
- `C:\ESPIDFprojects\ESP32_P4_BaseProject` — Guiton 4.3" ESP32-P4 base project

**Workspace:** `C:\ESPIDFprojects\RetroESP32_P4`

**Emulators Ported (10 total + 1 game):**

| # | Console | Emulator Core | OTA Slot | ROM Extensions | Status |
|---|---------|---------------|----------|---------------|--------|
| 1 | NES | nofrendo | ota_0 | .nes | ✅ Confirmed 60FPS |
| 2 | Game Boy / GBC | gnuboy | ota_1 | .gb, .gbc | ✅ Confirmed working |
| 3 | SMS / GG / COL | smsplus | ota_2 | .sms, .gg, .col | ✅ Built, should work |
| 4 | ZX Spectrum | spectrum | ota_3 | .z80, .sna | ✅ Working, crash fixes applied |
| 5 | Atari 2600 | stella | ota_4 | .a26, .bin | ✅ Built |
| 6 | Atari 7800 | prosystem | ota_5 | .a78 | ✅ Working, exit fix applied |
| 7 | Atari Lynx | handy | ota_6 | .lnx | ✅ Built |
| 8 | PC Engine | huexpress | ota_7 | .pce | ✅ Confirmed **60FPS** (async display) + save/load state |
| 9 | Atari 800 / 5200 | atari800 | ota_8 | .xex, .atr, .a52 | ✅ Confirmed **60FPS** (async audio, 5200 cart support) |
| 10 | OpenTyrian | opentyrian | ota_9 | .tyr | ✅ Working (standalone game) |
| 11 | **SNES** | snes9x | ota_10 | .smc, .sfc | ✅ Working, **45-67 FPS** (dual-core, 2× PPA, DKC crash fixed, save/load state) |

---

## 2. Hardware Specifications

| Component | Details |
|-----------|---------|
| **SoC** | ESP32-P4, RISC-V dual-core, 360 MHz |
| **Framework** | ESP-IDF v5.5.2 |
| **Flash** | 16 MB |
| **PSRAM** | 32 MB, 200 MHz hex mode |
| **Display** | 4.3" 480×800 MIPI DSI LCD (ST7701 controller) |
| **Scaler** | PPA (Pixel Processing Accelerator) 2D hardware engine |
| **Touch** | GT911 capacitive (not used by emulators) |
| **Audio** | ES8311 codec via I2S (I2C address 0x30) |
| **Input** | USB HID gamepad (NO GPIO buttons — affects safe boot design) |
| **SD Card** | SD MMC 4-bit, mounted at `/sd` |
| **Serial** | COM30, 115200 baud |

### GPIO Pin Map (from `pins_config.h`)

```
LCD Backlight:  GPIO 23 (LEDC PWM)
LCD Reset:      -1 (not connected)
Touch I2C:      SDA=7, SCL=8, RST=-1, INT=-1
I2S Audio:      MCLK=13, BCLK=12, WS=10, DOUT=9, DIN=48
Audio PA:       GPIO 11 (active high)
SD MMC:         CLK=43, CMD=44, D0=39, D1=40, D2=41, D3=42
```

### Controls

- **X button** = Home/Menu (opens in-game menu in emulators)
- **Y button** = Volume cycle (5 levels: Mute, 25%, 50%, 75%, 100%)
- **D-pad, A, B** = Standard gamepad
- **Start, Select** = Standard

---

## 3. Build Environment & Commands

### Setup ESP-IDF Environment (PowerShell)

```powershell
$env:IDF_PYTHON_ENV_PATH = "C:\Users\97254\.espressif\python_env\idf5.5_py3.11_env"
& "C:\Users\97254\esp\v5.5.2\esp-idf\export.ps1" 2>$null
```

### Build Everything

```powershell
.\build_all.ps1
```

This builds: launcher → `firmware/launcher.bin`, and all 7 emulator apps → `firmware/<name>_app.bin`

### Flash Everything

```powershell
.\flash_all.ps1
```

This flashes all binaries at correct offsets via `esptool` on COM30 at 460800 baud.

### Build Individual App

```powershell
# Example: build just the NES app
Push-Location apps\nes
idf.py build
Pop-Location
```

### Flash Individual Binary

```powershell
Stop-Process -Name python -Force -ErrorAction SilentlyContinue
Start-Sleep -Seconds 2
python -m esptool --chip esp32p4 -p COM30 -b 460800 --before default_reset --after hard_reset write_flash --flash_mode dio --flash_size 16MB --flash_freq 80m <offset> <path_to_bin>
```

### Serial Monitor

```powershell
python serial_capture.py   # outputs to monitor_*.txt, COM30 @ 115200
# OR
idf.py -p COM30 monitor
```

### Key sdkconfig Setting

All emulator apps share `apps/sdkconfig_common.defaults`:
```
CONFIG_ESP_MAIN_TASK_STACK_SIZE=32768
```
This was increased from 3584 to fix stack overflow crashes.

---

## 4. Architecture: OTA Multi-Binary

The project uses **ESP-IDF OTA partition switching** to run multiple independent applications on a single ESP32-P4:

```
┌──────────────────────────────────────────────────────┐
│  LAUNCHER (factory partition)                         │
│  - System carousel UI                                 │
│  - ROM browser with cover art                         │
│  - Favorites & recently played                        │
│  - Saves ROM path to NVS                              │
│  - Sets OTA boot partition → esp_restart()            │
└────────────────┬─────────────────────────────────────┘
                 │ OTA switch + reboot
    ┌────────────┴────────────┐
    ▼                         ▼
┌─────────┐  ┌─────────┐  ┌─────────┐  ...
│ NES App │  │ GB App  │  │ SMS App │
│ (ota_0) │  │ (ota_1) │  │ (ota_2) │
└────┬────┘  └────┬────┘  └────┬────┘
     │            │            │
     └────────────┴────────────┘
              │ On exit: set boot to factory → esp_restart()
              ▼
         Back to LAUNCHER
```

### Flow: Launcher → Emulator → Launcher

1. **Launcher** scans `/sd/roms/<subdir>/` for ROM files
2. User selects ROM → `rom_run()`:
   - Saves ROM path to NVS via `odroid_settings_RomFilePath_set()`
   - Saves resume state via `odroid_settings_DataSlot_set()` and `odroid_settings_StartAction_set()`
   - Maps extension → OTA slot via `get_ota_slot()`
   - Calls `esp_ota_set_boot_partition()` → `esp_restart()`
3. **Emulator app boots** → `app_main()`:
   - `app_init()` → NVS, crash guard, hardware, framebuffer, audio, gamepad, SD
   - `app_get_rom_path()` → reads ROM path from NVS
   - Runs emulator (blocks until user exits)
   - `app_return_to_launcher()` → sets factory partition → `esp_restart()`
4. **Launcher boots again** (back to step 1)

**Note:** Each transition is a full hardware reboot (`esp_restart()` → `rst:0xc SW_CPU_RESET`). This takes ~2-3 seconds per switch, which is expected/by-design.

---

## 5. Partition Table & Flash Map

File: `partitions_ota.csv`

| Name | Type | SubType | Offset | Size | Used By |
|------|------|---------|--------|------|---------|
| nvs | data | nvs | 0x9000 | 16 KB | NVS storage |
| otadata | data | ota | 0xD000 | 8 KB | OTA boot tracking |
| factory | app | factory | 0x10000 | 1 MB | Launcher (620 KB) |
| ota_0 | app | ota_0 | 0x110000 | 896 KB | NES/nofrendo (542 KB) |
| ota_1 | app | ota_1 | 0x1F0000 | 896 KB | GB-GBC/gnuboy (514 KB) |
| ota_2 | app | ota_2 | 0x2D0000 | 1.5 MB | SMS-GG-COL/smsplus (1206 KB) |
| ota_3 | app | ota_3 | 0x450000 | 896 KB | ZX Spectrum (574 KB) |
| ota_4 | app | ota_4 | 0x530000 | 1.5 MB | Atari 2600/stella (1157 KB) |
| ota_5 | app | ota_5 | 0x6B0000 | 896 KB | Atari 7800/prosystem (550 KB) |
| ota_6 | app | ota_6 | 0x790000 | 896 KB | Atari Lynx/handy (508 KB) |
| ota_7 | app | ota_7 | 0x870000 | 1.5 MB | *(future: PC Engine)* |
| ota_8 | app | ota_8 | 0x9F0000 | 1.5 MB | *(future: Atari 800)* |

### OTA Slot ↔ Extension Mapping (`get_ota_slot()` in launcher)

```
Slot 0 (ota_0): .nes           → nofrendo
Slot 1 (ota_1): .gb, .gbc      → gnuboy
Slot 2 (ota_2): .sms, .gg, .col → smsplus
Slot 3 (ota_3): .z80, .sna     → spectrum
Slot 4 (ota_4): .a26, .bin     → stella
Slot 5 (ota_5): .a78           → prosystem
Slot 6 (ota_6): .lnx           → handy
Slot 7 (ota_7): .pce           → huexpress (future)
Slot 8 (ota_8): .xex, .atr, .a52 → atari800
```

---

## 6. Display Pipeline (PPA)

> **IMPORTANT: Emulators scale to 640×480 (not full 800×480). Only the launcher uses the full 800×480 native resolution.**

The display uses PPA hardware acceleration with two distinct pipelines:

### Pipeline A: Standard Emulators (two-stage via 800×480 framebuffer)

Most emulators render to a 320×240 RGB565 virtual framebuffer. For non-native resolutions, PPA hardware scaling is used at Stage 1:

| Emulator | Native Res | Scale X | Scale Y | Method |
|----------|-----------|---------|---------|--------|
| NES | 256×224 | 1.25× | 1.071× | PPA scale → 320×240 FB |
| Game Boy | 160×144 | 2.0× | 1.667× | PPA scale → 320×240 FB |
| Game Gear | 160×144 | 2.0× | 1.667× | PPA scale → 320×240 FB |
| SMS | 256×192 | 1.25× | 1.25× | PPA scale → 320×240 FB |
| ColecoVision | 256×192 | 1.25× | 1.25× | PPA scale (via SMS path) |
| Atari 7800 | 320×240 | 1.0× | 1.0× | Direct write (native match) |
| Atari 2600 | 320×240 | 1.0× | 1.0× | Direct RGB565 memcpy |
| Atari Lynx | 160×102 | 2.0× | 2.353× | PPA scale → 320×240 FB |
| C64 | 384×272 | crop | crop | CPU crop 384×272→320×240 center |

Stage 2 copies 320×240 into 800×480 framebuffer (with borders), then PPA rotates 270° → 480×800:

```
s_framebuffer (800×480 RGB565, 320×240 centered with borders)
    │
    ▼ PPA rotate 270° CCW (no scaling, 1:1)
    │
s_ppa_out_buf (480×800 RGB565)
    │
    ▼ st7701_lcd_draw_rgb_bitmap(0, 0, 480, 800)
    │
Physical LCD (480×800 MIPI DSI)
```

### Pipeline B: ZX Spectrum (optimized direct PPA path)

**ZX Spectrum bypasses the 800×480 framebuffer entirely.** PPA does 2× scale + 270° rotation + byte-swap in a single hardware operation directly from the 320×240 emulator output:

```
Emulator 320×240 RGB565 (BE, in PSRAM)
    │
    ▼ PPA: scale 2× + rotate 270° + byte-swap (single HW op)
    │
s_ppa_out_buf (480×640 RGB565)
    │
    ▼ st7701_lcd_draw_rgb_bitmap(0, 80, 480, 640)
    │
Physical LCD (480×800, 80px black bars top+bottom)
```

**Performance comparison (PPA time per frame):**
| Path | PPA Input | PPA Time | FPS |
|------|-----------|----------|-----|
| Old (via 800×480 FB) | 800×480 = 384K pixels | 19.4ms | 41.3 |
| New (direct 320×240) | 320×240 = 77K pixels | **5.3ms** | **50.1** |

**The direct path is 3.7× faster** because PPA processes 5× fewer pixels.

### Launcher Pipeline (native 800×480)

The launcher writes directly to an 800×480 framebuffer at native resolution. PPA does rotation only (no scaling). See Phase 17.

### Buffer Allocations

| Buffer | Size | Location | Purpose |
|--------|------|----------|---------|
| `s_framebuffer` | 768,000 bytes | PSRAM+DMA, 64-byte aligned | 800×480 RGB565 virtual FB (standard emulators + launcher) |
| `s_ppa_out_buf` | 768,000 bytes | PSRAM+DMA, 64-byte aligned | 480×800 max rotated+scaled output |
| `s_nes_temp` | 114,688 bytes | PSRAM+DMA, 64-byte aligned | NES 256×224 RGB565 temp |
| `s_gb_temp` | 46,080 bytes | PSRAM+DMA, 64-byte aligned | GB 160×144 RGB565 temp |
| `s_sms_temp` | 98,304 bytes | PSRAM+DMA, 64-byte aligned | SMS 256×192 RGB565 temp |
| `s_lynx_temp` | 32,640 bytes | PSRAM+DMA, 64-byte aligned | Lynx 160×102 RGB565 temp |

All static buffers are lazy-allocated on first use and persist for the app lifetime.

> **Note:** The ZX Spectrum direct path (`ili9341_write_frame_rgb565_ex()`) does not use `s_framebuffer` — it goes straight from the emulator's double-buffer to PPA to LCD.

---

## 7. Key Source Files

### Shared Components

| File | Lines | Purpose |
|------|-------|---------|
| `components/app_common/app_common.c` | ~197 | Shared init/exit for all OTA emulator apps |
| `components/app_common/app_common.h` | - | API: `app_init()`, `app_return_to_launcher()`, `app_check_safe_boot()`, `app_get_rom_path()` |
| `components/odroid/odroid_display.c` | ~486 | Virtual 320×240 framebuffer + PPA display pipeline |
| `components/odroid/odroid_system.c` | ~122 | Hardware init: I2C → PPA → USB → Audio → LCD → Touch |
| `components/ppa_engine/ppa_engine.c` | ~568 | PPA hardware wrapper: rotate, scale, combined ops |
| `components/odroid/odroid_audio.c` | - | Audio output (I2S to ES8311) |
| `components/odroid/odroid_input.c` | - | Gamepad input (USB HID polling) |
| `components/odroid/odroid_sdcard.c` | - | SD card mount/unmount |
| `components/odroid/odroid_settings.c` | - | NVS settings (volume, ROM path, etc.) |

### Launcher

| File | Lines | Purpose |
|------|-------|---------|
| `launcher/main/main.c` | ~3506 | Full launcher: carousel UI, ROM browser, PNG artwork, OTA switching |
| `launcher/main/sprites/font8x16.h` | ~1600 | VGA-style 8×16 bitmap font (ASCII 32–126) |
| `launcher/main/pins_config.h` | ~34 | GPIO pin definitions |

### Emulator Apps (all follow same pattern)

| File | OTA | Entry Point |
|------|-----|------------|
| `apps/nes/main/main.c` | ota_0 | `nofrendo_run(rom_path)` |
| `apps/gb/main/main.c` | ota_1 | `gnuboy_run(rom_path)` |
| `apps/sms/main/main.c` | ota_2 | `smsplus_run(rom_path)` |
| `apps/spectrum/main/main.c` | ota_3 | `spectrum_run(rom_path)` |
| `apps/stella/main/main.c` | ota_4 | `stella_run(rom_path)` |
| `apps/prosystem/main/main.c` | ota_5 | `prosystem_run(rom_path)` |
| `apps/handy/main/main.c` | ota_6 | `handy_run(rom_path)` |
| `apps/pce/main/main.c` | ota_7 | `huexpress_run(rom_path)` |
| `apps/atari800/main/main.c` | ota_8 | `atari800_run(rom_path)` |
| `apps/opentyrian/main/main.c` | ota_9 | `opentyrian_run()` |

**All emulator app main.c files follow this exact pattern:**
```c
void app_main(void) {
    app_init();                              // Hardware + crash guard
    char rom_path[256];
    if (app_get_rom_path(...) != 0) {        // Read NVS
        app_return_to_launcher();             // No ROM → go back
    }
    <emulator>_run(rom_path);                // Run emulator (blocks)
    app_return_to_launcher();                // Exit → go back
}
```

### Emulator Core Components

| Component | Directory | Required By |
|-----------|-----------|------------|
| nofrendo | `components/nofrendo/` | NES app |
| gnuboy | `components/gnuboy/` | GB app |
| smsplus | `components/smsplus/` | SMS app |
| spectrum | `components/spectrum/` (assumed in components) | Spectrum app |
| stella | `components/stella/` (assumed in components) | Stella app |
| prosystem | `components/prosystem/` (assumed in components) | ProSystem app |
| handy | `components/handy/` (assumed in components) | Handy app |
| huexpress | `components/huexpress/` | PCE app — `huexpress_run.c` (save/load state, menu, async display), `engine/gfx.h`/`gfx.c`/`gfx_Loop6502.h` (rendering + CR protection), `engine/pce.c`/`pce.h` (21 standalone VDC vars via `MY_VDC_VARS`) |
| atari800 | `components/atari800/` | Atari 800 app |
| opentyrian | `components/opentyrian/` | OpenTyrian app |

---

## 8. Emulator Apps & Launcher

### Launcher Carousel Entries (13 total)

| Index | Name | Directory | Extension |
|-------|------|-----------|-----------|
| 0 | SETTINGS | (special) | - |
| 1 | FAVORITES | (special) | - |
| 2 | RECENTLY PLAYED | (special) | - |
| 3 | NINTENDO ENTERTAINMENT SYSTEM | nes | .nes |
| 4 | NINTENDO GAME BOY | gb | .gb |
| 5 | NINTENDO GAME BOY COLOR | gbc | .gbc |
| 6 | SEGA MASTER SYSTEM | sms | .sms |
| 7 | SEGA GAME GEAR | gg | .gg |
| 8 | COLECOVISION | col | .col |
| 9 | ATARI 7800 | a78 | .a78 |
| 10 | ZX SPECTRUM | spectrum | .z80 |
| 11 | ATARI 2600 | a26 | .a26 |
| 12 | ATARI LYNX | lynx | .lnx |

### In-Game Menu (all emulators)

Press **X (Menu)** during gameplay:
- **Resume Game** — dismiss menu
- **Restart Game** — reset emulator
- **Save Game** / **Overwrite Save** — save state to SD
- **Reload Game** — load state from SD
- **Delete Save** — remove save file
- **Exit Game** — auto-saves, returns to launcher

Navigation: D-pad UP/DOWN, A to select, B/Menu to dismiss.

---

## 9. Crash Guard System

**File:** `components/app_common/app_common.c`

The crash guard protects against boot loops when an emulator crashes repeatedly:

### How It Works

1. On boot: `crash_guard_check()` reads NVS counter (`app_guard/boot_cnt`)
2. If counter >= 3 → **automatic rollback to factory (launcher)**
3. Otherwise → increment counter, save to NVS
4. After `app_init()` completes: start a **10-second one-shot FreeRTOS timer**
5. When timer fires → `crash_guard_clear()` resets counter to 0
6. If emulator crashes before 10s → counter stays incremented → detected on next boot

### Why 10-Second Timer (Not Immediate Clear)

The original implementation cleared the counter immediately after `app_init()`. But the real crashes happened in the emulator code AFTER init (e.g., NULL framebuffer deref). The delayed timer ensures the emulator actually runs successfully for 10 seconds before declaring "no crash."

### NVS Namespace & Key

```
Namespace: "app_guard"
Key:       "boot_cnt"
Type:      uint8_t
Max:       3 (CRASH_GUARD_MAX)
```

---

## 10. Completed Work — All Phases

### Phase 1: Launcher
- [x] Ported RetroESP32 launcher UI to ESP32-P4
- [x] System carousel with ROM browser
- [x] Favorites and recently played lists
- [x] Cover art display
- [x] OTA partition switching for emulator launch

### Phase 2: NES (nofrendo)
- [x] Ported nofrendo NES emulator
- [x] PPA hardware scaling (256×224 → 320×240)
- [x] Fixed NES resume crash (save/load state)
- [x] Fixed NES resource leaks on exit
- [x] Fixed libsnss memory leak
- [x] In-game menu + volume overlay
- [x] Save/load works from launcher

### Phase 3: Game Boy / GBC (gnuboy)
- [x] Ported gnuboy emulator
- [x] Fixed `IS_LITTLE_ENDIAN` for RISC-V
- [x] Fixed gnuboy black screen issue
- [x] PPA hardware scaling for GB display
- [x] Removed frame skip for smooth display
- [x] In-game menu + volume overlay

### Phase 4: SMS / GG / COL (smsplus)
- [x] Ported smsplus emulator
- [x] Fixed Z80 crash (PC overflow — masked to 16 bits)
- [x] Fixed LSB_FIRST endianness detection
- [x] Fixed old save state crash (PAIR byte-swap in state.c)
- [x] Fixed wrong colors (removed erroneous byte-swap)
- [x] PPA hardware scaling
- [x] Non-blocking video queue
- [x] Dynamic save subdirectory by ROM extension

### Phase 5: Additional Emulators
- [x] Atari 7800 (prosystem) — native 320×240, direct framebuffer write
- [x] ZX Spectrum — direct RGB565 output
- [x] Atari 2600 (stella) — direct RGB565 output
- [x] Atari Lynx (handy) — direct RGB565 output

### Phase 6: OTA Multi-Binary Architecture
- [x] Partition table with factory + 9 OTA slots
- [x] `app_common` shared component for all emulator apps
- [x] `build_all.ps1` and `flash_all.ps1` scripts
- [x] All 7 emulator apps as separate IDF projects under `apps/`

### Phase 7: Critical Bug Fixes
- [x] Stack overflow fix (3584 → 32768 bytes)
- [x] NULL framebuffer crash fix (`ili9341_init()` added to `app_init()`)
- [x] Crash guard timing fix (immediate clear → 10-second timer)
- [x] PPA scaling restored (was wrongly blamed; NULL FB was the real issue)

### Phase 8: In-Game Menus & Controls (All Emulators)
- [x] Unified in-game menu system across all emulators (X button = menu)
- [x] Menu options: Resume, Restart, Save State, Reload State, Overwrite Save, Delete Save, Exit Game
- [x] Dynamic option visibility based on save file existence
- [x] Volume overlay with Y button cycling (Mute → 25% → 50% → 75% → 100%)
- [x] Volume bar rendered with colored gradient (green → cyan → yellow)
- [x] 5×7 bitmap font for OSD text rendering (shared pattern across all emulators)
- [x] ZX Spectrum virtual keyboard overlay (4×10 grid with CS/SS shift toggle)
- [x] Flash confirmation messages ("Saved!", "Loaded!", "Deleted!", "Error!")
- [x] All menus auto-dismiss on button release to prevent input bleed

### Phase 9: PPA Engine Fixes
- [x] Fixed PPA S→R→M ordering bug in `ppa_rotate_scale_rgb565_to()`
- [x] PPA hardware executes Scale→Rotate→Mirror; for 90°/270° rotations the pre-scale axes must be swapped so post-rotation dimensions are correct
- [x] Output buffer size check to prevent overflows

### Phase 10: Launcher Browser Fixes
- [x] Fixed browser state clamping on return from game (`ROMS.offset`, `BROWSER_SEL`)
- [x] Prevented crash if ROM list changed between sessions (offset > total)
- [x] Same clamping applied to Favorites and Recents browsers
- [x] Pre-count all ROMs per system at boot for instant page counters

### Phase 11: OpenTyrian Integration
- [x] Ported OpenTyrian (shoot-'em-up) as standalone game on ota_9 slot
- [x] Custom PPA scale override: `display_set_scale(2.0f, 2.5f)` — fills 480×800 LCD from 320×200 native
- [x] Launcher watchdog task on core 1 — 3s MENU hold returns to launcher
- [x] Game data loaded from `/sd/tyrian/data/` on SD card
- [x] Launcher carousel entry (STEP==15) with no ROM browser (direct launch)

### Phase 12: ZX Spectrum Crash Fixes
- [x] Fixed ZX Spectrum crashes during gameplay
- [x] Double-buffered display: `lcdfb[2]` — RGB565 320×240, PSRAM allocated
- [x] Save/load state: `snsh_save()`/`load_snapshot_file_type()` to `/sd/odroid/data/spectrum/<rom>.sav`
- [x] Full 7-option in-game menu consistent with other emulators

### Phase 13: Atari 7800 Exit & Menu Fix
- [x] Fixed Atari 7800 exit button handling (edge-triggered MENU button)
- [x] Full 7-option in-game menu with save/load state (32829-byte state buffer in PSRAM)
- [x] Frame skip: `RenderFlag = frame & 1` — renders every other frame for performance
- [x] Double-buffered display with `xQueueOverwrite` for non-blocking video submission
- [x] Clean resource teardown on exit: framebuffers, palette, sample buffer, video queue

### Phase 14: PC Engine Performance Optimization (41 FPS → 60 FPS)
- [x] **Profiled full frame pipeline** — identified bottleneck breakdown:
  - Baseline: EMU=14.4ms, PAL=4.0ms, PPA=5.4ms, LCD=0.2ms → 24.0ms total = **41.7 FPS**
- [x] **Fixed double-sleep bug** in `utils.c` — removed redundant `select()` call, kept `usleep()` only
- [x] **Enabled -O2 optimization** for PCE app (`CONFIG_COMPILER_OPTIMIZATION_PERF=y`)
- [x] **Async double-buffered display pipeline** — moved PAL+PPA+LCD to a FreeRTOS task on core 1:
  - Two 320×240 8-bit centering framebuffers (`disp_fb[2]`) in PSRAM
  - Palette snapshot (`disp_pal_snap`) copied per-frame, consumed by display task
  - Binary semaphore handshake: `disp_go_sem` (frame ready) + `disp_ready_sem` (display done)
  - `pce_display_task()` pinned to core 1 (priority 6), audio task on core 1 (priority 5)
  - **Result: 41 FPS → 55 FPS** — display overlaps completely with emulation
- [x] **Optimized palette conversion** — 32-bit word access reads 4 pixels per iteration, packs 2 RGB565 outputs per write, reduces PSRAM cache line fetches
- [x] **Internal SRAM placement** to eliminate PSRAM cache contention between cores:
  - `disp_pal_snap` (512 bytes) → `MALLOC_CAP_INTERNAL` — palette lookups at SRAM speed
  - `s_framebuffer` (153,600 bytes) → tries `MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA` first, PSRAM fallback
  - **Result: 55 FPS → 60.0 FPS** — rock solid, WAIT=0ms, zero drops across 70+ seconds
- [x] **Added timing instrumentation** to `odroid_display.c` — PPA/LCD/PAL per-stage timing every 60 frames

### Phase 15: Atari 800 Performance Optimization (52 FPS → 60 FPS)
- [x] **Profiled full frame pipeline** — identified bottleneck breakdown:
  - Baseline: EMU=4.7ms, VID=1.2ms, SND_GEN=0.7ms, **SND_SUBMIT=13.0ms** → 19.1ms total = **52.5 FPS**
  - Audio I2S blocking (`odroid_audio_submit()` → `i2s_channel_write(portMAX_DELAY)`) consumed 68% of frame time
- [x] **Enabled -O2 optimization** for Atari 800 app (`CONFIG_COMPILER_OPTIMIZATION_PERF=y`)
- [x] **Switched PAL → NTSC** — `Atari800_tv_mode = Atari800_TV_NTSC` in `atari.cpp`
- [x] **Async audio task** — moved I2S submission to a dedicated FreeRTOS task on core 1:
  - `audioTask` pinned to core 1 (priority 4), receives `audio_msg_t` via queue
  - Double-buffered `sampleBuffer[2]` — emulation fills one while audio task drains the other
  - `audioQueue` depth 2 — provides back-pressure without stalling emulation
  - `emu_step()` now non-blocking: queue send instead of direct `odroid_audio_submit()`
  - Proper cleanup: poison-pill (`NULL` buffer) stops audio task on exit
- [x] **Fixed sample count** — changed from 302 samples/frame (matched 52 FPS rate) to `AUDIO_SAMPLE_RATE / 60` = 262 samples/frame, matching 60 FPS NTSC target
- [x] **Result: 52 FPS → 60 FPS** — audio no longer blocks the emulation loop

#### Atari 800 Optimization Summary Table

| Stage | EMU | VID | SND Gen | SND Submit | Total | FPS |
|-------|-----|-----|---------|------------|-------|-----|
| Baseline (blocking audio) | 4.7ms | 1.2ms | 0.7ms | **13.0ms** | 19.1ms | **52.5** |
| + Async audio + 262 samp | 4.7ms | 1.2ms | 0.7ms | ~0ms | ~6.6ms | **60.0** |

### Phase 16: PC Engine (HuExpress) Save/Load State
- [x] **Binary save format v4** — "PCES" magic header + version tag, saved to `/sd/odroid/data/pce/<romname>.sav`
- [x] **Full 7-option in-game menu** — Resume, Restart, Save State, Reload State, Overwrite Save, Delete Save, Exit Game (consistent with all other emulators)
- [x] **Auto-save on exit** — state saved automatically when exiting to launcher
- [x] **Auto-load on resume** — when launcher sends `StartAction == resume`, state restores seamlessly
- [x] **Post-`gfx_init()` callback** — `pce_post_gfx_init_cb` fires after `gfx_init()` zeroes VDC registers, loads state at the right moment before the main loop
- [x] **`MY_VDC_VARS` discovery & fix** — the engine uses 21 standalone `pair` variables (`IO_VDC_00_MAWR` … `IO_VDC_14`) as the live VDC registers, NOT the `io.VDC[32]` array (which is dead memory). All 21 standalone vars are now saved/restored.
- [x] **Post-load fixups:**
  - `IO_VDC_active_set(io.vdc_reg)` — restores active VDC register pointer
  - `bg_w`/`bg_h` derived from `IO_VDC_09_MWR` (background map dimensions)
  - `vdc_inc` derived from `IO_VDC_05_CR` (VRAM auto-increment)
  - `screen_w` derived from `IO_VDC_0B_HDR` (horizontal display register)
  - `gfx_need_video_mode_change = 1` — triggers video mode recalculation
  - Full tile-cache rebuild (`vchange`/`vchanges` memset to 1)
  - `SetPalette()` — rebuilds RGB palette from VCE data
- [x] **VBLANK CR protection** — games zero VDC register 5 (control register) during VBLANK for DMA. `pce_display_cr` captures the true CR value at display start. On save, `IO_VDC_05_CR` is patched with this value before writing. On load, CR protection counter (120 frames / ~2s) overrides the zeroed CR until the game's normal rendering loop stabilizes.
- [x] **CPU register handling** — uses standalone globals (`reg_pc_`, `reg_a_`, `reg_x_`, `reg_y_`, `reg_p_`, `reg_s_`) per `hard_pce.h` non-SHARED_MEMORY path, plus `cycles_` counter
- [x] **State contents:** CPU regs, cycle counters, MMR bank mappings, RAM (32K), WRAM (8K), VRAM (64K), SPRAM, palette, tile cache flags, full IO struct, 21 standalone VDC vars, VDC ancillary state, joypad, PSG (6-channel + DA buffers), timer/IRQ, GFX context, `gfx_need_redraw`

### Phase 17: Launcher Native 800×480 UI Overhaul
- [x] **Full native 800×480 resolution rewrite** — launcher framebuffer writes directly to 800×480 display via `ili9341_write_frame_rectangleLE()`, bypassing the emulator 320×240 → PPA 2× upscale pipeline
- [x] **VGA-style 8×16 bitmap font** — `font8x16.h` with 95 ASCII glyphs (32–126), public domain VGA patterns
- [x] **`draw_text()` at 2× scale** — 16×32 output glyphs (20px advance), used for headers, system names, browser list
- [x] **`draw_text_scaled()` at 3× scale** — 24×48 output glyphs (28px advance), used for carousel hints
- [x] **2× software upscale for system icons** — 16×16 sprites scaled to 32×32 for battery, speaker, brightness icons
- [x] **Per-emulator PNG artwork** — `show_system_artwork()` loads `/sd/system_art/{name}.png` (400×225 images)
  - 2× software upscale to 800×450, displayed at y=30..479
  - Uses `loadPngFromFileRaw()` for raw RGB565 decode from PNGdec
  - Byte-swap + R↔B channel swap for correct colors: `bs = (p>>8)|(p<<8)` then `((bs & 0x1F) << 11) | (bs & 0x07E0) | ((bs >> 11) & 0x1F)`
  - 30px black header stripe at y=0 with yellow file count (left) and battery/volume/brightness icons (right) drawn with black background
  - Supports favorites (`favorite.png`) and recents (`recent.png`) artwork
  - Falls back to `draw_system_logo()` (6× scaled monochrome icon) if PNG not found
- [x] **Conditional hint text** — "press a to browse" only shown when artwork fails to load (fallback mode)
- [x] **Settings screen cleanup** — `animate()` STEP==0 clears full 480px area before redrawing to prevent artwork leftovers
- [x] **Browser header redesign** — `draw_browser_header()` uses 800×48 clear mask, system name left + page info right, no status icons
- [x] **Icons removed from browser** — `debounce()` no longer draws battery/speaker/contrast icons (was causing icons to appear over browser text)
- [x] **Icon background color consistency** — removed redundant icon draws from `draw_carousel_screen()` and `launcher()` entry that were overwriting artwork's black-bg icons with `GUI.bg`-colored backgrounds
- [x] **`draw_system_logo()` fallback** — 6× scaled (192×192) monochrome system icon centered on screen when no PNG artwork exists
- [x] **Progress bar redesign** — battery level indicator uses 11-entry color gradient array (red → yellow → green)

#### Launcher Display Pipeline (Native 800×480)

The launcher bypasses the emulator display pipeline entirely:
```
Launcher draw functions
    │ ili9341_write_frame_rectangleLE(x, y, w, h, buffer)
    ▼
800×480 framebuffer (direct pixel writes)
    │ display_flush()
    ▼ PPA rotate 270° (no scaling — already native size)
    │
480×800 MIPI DSI LCD (st7701_lcd_draw_rgb_bitmap)
```

Key difference from emulators: launcher writes to 800×480 framebuffer at native resolution and uses PPA rotation only (no 2× scaling). Emulators write to 320×240 and use PPA for both 2× scale + 270° rotation.

#### PNG Artwork Color Pipeline

PNGdec outputs big-endian RGB565 on RISC-V. The MIPI DSI panel (ST7701, `LCD_RGB_ELEMENT_ORDER_RGB`, MADCTL=0x00) expects little-endian with BGR channel order. The fix requires two operations:
1. **Byte-swap:** `bs = (p >> 8) | (p << 8)` — converts big-endian → little-endian
2. **R↔B swap:** `((bs & 0x1F) << 11) | (bs & 0x07E0) | ((bs >> 11) & 0x1F)` — swaps red and blue channels

This was determined empirically over 5 iterations: BGR swap only → raw (no swap) → R↔B only → byte-swap only → byte-swap + R↔B (correct).

#### Bugs fixed during launcher UI overhaul:
1. **PNG colors wrong** — PNGdec big-endian RGB565 vs panel little-endian BGR565 (5 iterations to fix)
2. **Icons appearing in browser** — `debounce()` drew icons on every button press, including in browser mode
3. **Icon background color mismatch** — `GUI.bg` (theme color, often white) vs `0x0000` (black) from artwork header; redundant icon draws after `restore_layout()` overwrote artwork icons
4. **Settings screen leftovers** — artwork pixels remained when scrolling to settings (STEP==0); fixed by clearing full 480px area

### Phase 19: SNES (snes9x) Integration & Optimization (42 → 50 FPS)
- [x] **snes9x core ported** — full CPU/PPU/APU/DSP emulation of the Super Nintendo. ROM loaded from SD card into PSRAM. Pixel format: snes9x outputs `RRRRR GGGGG 0 BBBBB` (GREEN_SHIFT=6, 5-bit green), converted to standard RGB565 via `px | ((px >> 5) & 0x0020)` in the video task
- [x] **Display pipeline: direct 2× PPA scaling** — native 256×224 → 512×448 via `ili9341_write_frame_rgb565_custom()` with `scale=2.0`. No intermediate 320×240 staging buffer. PPA does 2× scale + 270° rotation in a single HW operation. VID cost: **0.0ms** on Core 0 (fully offloaded to Core 1 video task)
- [x] **Audio offloaded to Core 1** — `snes_audio_task()` on Core 1 (priority 6) receives task notification from emu loop, calls `S9xMixSamples()` + `odroid_audio_submit()` entirely on Core 1. AUD cost on Core 0: **0.0ms** (was ~1ms blocking). 32000 Hz stereo, double-buffered DMA
- [x] **DSP optimizations** — `DisableSoundEcho=true` (skips echo buffer, FIR filter, feedback), `InterpolatedSound=false` (cheaper sample mixing)
- [x] **In-game menu** — 5×5 bitmap font, "Resume" / "Restart" / "Exit" options, MENU button trigger. Draws directly into SNES framebuffer (256×224)
- [x] **Compiler optimization** — snes9x component compiled with `-O3`, framework with `-O2` (`CONFIG_COMPILER_OPTIMIZATION_PERF=y`)
- [x] **Frame skip** — adaptive frame drop based on frame time budget (16.7ms target). ~12 skip per 95 frames in heavy scenes
- [x] **Blargg APU tested and rejected** — cycle-accurate DSP resampling was heavier than the old per-instruction SPC700 (27ms vs 21ms/frame). Reverted to old APU
- [x] **400MHz overclock tested and rejected** — ESP32-P4 at 400MHz degrades PSRAM/flash bus timings. CPU went from 21ms to 32ms/frame (memory-bound workload). Reverted to 360MHz

#### SNES Optimization Summary Table

| Metric | Baseline | After Optimization | Improvement |
|--------|----------|-------------------|-------------|
| CPU (Core 0) | 21ms | 18-21ms | Lighter scenes improved |
| AUD (Core 0) | ~1ms (blocking) | **0.0ms** | Fully offloaded to Core 1 |
| VID (Core 0) | ~0.5ms | **0.0ms** | Offloaded to Core 1 |
| Total/frame | ~22.5ms | 18-21ms | 1.5-4.5ms saved |
| **FPS (gameplay)** | **42-45** | **45-50** | **+5 FPS** |
| FPS (lighter scenes) | — | 54 | Near 60 target |
| FPS (menus/loading) | — | 75-81 | Blazing |
| Display path | 320×240 staging → PPA | Direct 2× PPA (256×224→512×448) | Eliminated staging buffer |
| Audio path | Blocking `odroid_audio_submit()` | Core 1 task notification | Non-blocking on Core 0 |

> Remaining bottleneck: `S9xMainLoop()` at 18-21ms — SPC700 APU executes per-instruction via `APU_EXECUTE` macro on every CPU cycle. This is an architectural limitation of this snes9x fork. Scanline-based batch APU sync was attempted but broke emulator timing.

### Phase 20: SNES DKC Crash Fix & Stability Hardening

**Problem:** Donkey Kong Country crashed with `Guru Meditation Error: Core 0 panic'ed (Load access fault)` every time the player entered a gameplay level (title screen worked fine). The crash was deterministic:
- MCAUSE=0x00000005, MTVAL=0x00010820, PC=0x4ff03a38 (in `WRITE_4PIXELS16_SUBF1_2`)
- Crash occurred in fixed-colour subtraction rendering — DKC's level transitions use SNES mode-5 colour math (translucent overlay effects)

**Investigation journey (5+ attempts):**
1. ❌ `CONFIG_ESP_MAIN_TASK_STACK_SIZE=65536` — killed boot entirely (no contiguous 64KB in internal RAM alongside 173KB IRAM code)
2. ❌ `IPPU.Interlace = false` in `S9xStartScreenRefresh()` — didn't fix crash (interlace wasn't the cause)
3. ❌ PSRAM-backed 64KB emulator task via `xTaskCreateStaticPinnedToCore` — crash persisted, but revealed SP moved to `0x480d9b30` (PSRAM), proving it wasn't a stack overflow
4. ❌ Z-buffer pointer validation guards in `S9xStartScreenRefresh()` / `S9xUpdateScreen()` — GFX.ZBuffer/SubZBuffer were valid at crash time
5. ✅ **Root cause found via disassembly** — `objdump` showed the faulting instruction was `lhu a3, 0(a3)` (a `uint16_t` load), NOT a Z-buffer byte access. The computed address `0x00010820` came from the `COLOR_SUB1_2` macro dereferencing `GFX.ZERO` — a 128KB lookup table that was **never allocated**

**Root cause detail:**
- `GFX.ZERO` is a 64K-entry `uint16_t` LUT used by the `COLOR_SUB1_2` macro for colour subtraction with half-brightness
- The allocation code in `S9xInitGFX()` (`gfx.c:188`) was wrapped in `#if 0` — intentionally disabled to save 128KB of RAM
- Most games never trigger `COLOR_SUB1_2` (it requires fixed-colour subtraction mode), so the NULL dereference never fired
- DKC's level transitions set PPU registers `$2130`/`$2131` to mode `0xC0` (fixed colour subtraction), which selects `DrawTile16FixedSub1_2` → calls `COLOR_SUB1_2(ScreenColors[Pixel], GFX.FixedColour)` → `GFX.ZERO[computed_index]` → crash at NULL + offset

**Fix applied (3 parts):**
1. **`NO_ZERO_LUT` compile definition** — added `target_compile_definitions(${COMPONENT_LIB} PUBLIC NO_ZERO_LUT)` in `components/snes9x/CMakeLists.txt`. This switches `COLOR_SUB1_2` from the broken LUT macro to an inline function that computes the result mathematically (already existed in `gfx.h` behind `#ifdef NO_ZERO_LUT`)
2. **512-wide Z-buffer allocation** — changed Z-buffer allocation from `SNES_FB_W (256) × SNES_FB_H_EXT` to `512 × SNES_FB_H_EXT` in `S9xInitDisplay()` (`snes_run.c`). Provides safety margin for mode 5/6 rendering which sets `RenderedScreenWidth = 512`
3. **Pointer validation guards** — added `saved_ZBuffer` / `saved_SubZBuffer` pristine pointer copies (defined in `gfx.c`). `S9xStartScreenRefresh()` and `S9xUpdateScreen()` check and repair corrupted pointers before rendering (defense in depth)

**Files modified:**
- `components/snes9x/CMakeLists.txt` — added `NO_ZERO_LUT` definition
- `components/snes9x/gfx.c` — added `saved_ZBuffer`/`saved_SubZBuffer` globals, pointer validation in `S9xStartScreenRefresh()` and `S9xUpdateScreen()`
- `apps/snes/main/snes_run.c` — 512-wide Z-buffer allocation, pristine pointer save, cleanup

**Test result:** DKC ran 120 seconds continuously (55 profile windows, 45-67 FPS) including level entry, gameplay, and scene transitions — zero crashes.

### Phase 21: SNES Save/Load State

**Goal:** Implement save state functionality for the SNES emulator, matching the pattern used by PCE (HuExpress).

**Background:** The snes9x component had vestigial save/load code in `save.c` that was 100% disabled (`#if 0` guards with an `#error Turn SRAM back on` directive). The original code attempted to serialize raw `APU`/`IAPU` structs (which contain pointers — invalid on reload) and was never wired to any UI.

**Implementation:**

1. **`components/snes9x/save.c`** — Complete rewrite:
   - `S9xSaveState(const char *filename)` — serializes full emulator state to file:
     - Header: `"SNES9X_000000003"` (16 bytes)
     - CPU state: `CPU` (SCPUState) + `ICPU` (SICPU) structs
     - Graphics: `PPU` (SPPU) + `DMA[8]` (SDMA × 8)
     - Memory: VRAM (64KB) + RAM (128KB) + SRAM (128KB) + FillRAM (32KB)
     - Audio: `APU` (SAPU) + `IAPU` (SIAPU) + IAPU.RAM (64KB) + `SoundData` (SSoundData)
   - `S9xLoadState(const char *filename)` — deserializes + fixes up:
     - IAPU pointer rebasing (PC, DirectPage, WaitAddress1, WaitAddress2 — all relative to IAPU.RAM base)
     - Full state repair chain: `FixROMSpeed()`, `S9xFixColourBrightness()`, `S9xAPUUnpackStatus()`, `S9xFixSoundAfterSnapshotLoad()`, `S9xSetPCBase()`, `S9xUnpackStatus()`, `S9xFixCycles()`, `S9xReschedule()`
   - Uses classic (non-blargg) APU — project does NOT define `USE_BLARGG_APU`
   - Total save file size: ~480KB (header + CPU/PPU/DMA structs + 288KB memory + APU/sound state)

2. **`components/snes9x/save.h`** — Updated function signatures to accept `const char *filename`

3. **`apps/snes/main/snes_run.c`** — Menu expansion + SD card integration:
   - `snes_get_save_path()` — derives `/sd/odroid/data/snes/<romname>.sav` from NVS ROM path
   - `snes_check_save_exists()` — stat check for file existence
   - `snes_do_save_state()` / `snes_do_load_state()` — opens SD, creates directory hierarchy, calls save.c functions
   - Menu expanded from 2 → 4 items: **RESUME**, **SAVE STATE**, **LOAD STATE**, **EXIT GAME**
   - "LOAD STATE" greyed out (0x4208 dark grey) when no save file exists
   - Status feedback displayed below menu box: "SAVED OK" (green) / "SAVE FAILED" (red) / "LOAD FAILED" (red)
   - After successful load, menu auto-resumes (returns to gameplay with restored state)

**Key design decisions:**
- Uses classic APU serialization (raw `SAPU`/`SIAPU` struct dump + 64KB IAPU.RAM), NOT blargg APU state copy, since `USE_BLARGG_APU` is not defined in this build
- IAPU contains pointer fields (PC, DirectPage, WaitAddress1, WaitAddress2) that are saved as absolute addresses — on load, they're rebased: `ptr = saved_ptr - saved_RAM_base + current_RAM_base`
- CPU and ICPU also contain pointer fields (PC, PCBase, S9xOpcodes) — fixed up by `S9xSetPCBase()` and `S9xFixCycles()` after load
- Save file stored on SD card (not flash) following the same `/sd/odroid/data/<emu>/` convention as PCE

**Files modified:**
- `components/snes9x/save.c` — full rewrite (was 100% dead code)
- `components/snes9x/save.h` — added `const char *filename` parameter
- `apps/snes/main/snes_run.c` — save path helpers, expanded menu (2→4 items), save.h include

**Test result:** Save state confirmed working — save writes to SD, load restores gameplay state correctly.

### Phase 18: ZX Spectrum Full Optimization (41 FPS → 50 FPS)
- [x] **Compiler optimization: `-Os` → `-O2` + component-level `-O3`** — sdkconfig changed to `CONFIG_COMPILER_OPTIMIZATION_PERF=y` (global `-O2`), plus `target_compile_options(${COMPONENT_LIB} PRIVATE -O3)` in spectrum component CMakeLists.txt for maximum Z80 emulation speed
- [x] **Kempston joystick support** — gamepad now drives **both** Cursor/Protek keys (5,6,7,8,0) AND Kempston I/O port (0x1F) simultaneously. D-pad→directions, A/B→fire. Games using either joystick standard now work without user configuration
- [x] **PPA direct pipeline: 320×240 → 480×640 in single HW operation** — eliminated the intermediate 800×480 framebuffer entirely. PPA does 2× scale + 270° rotation + byte-swap in one call via `ili9341_write_frame_rgb565_ex(buffer, byte_swap=true)`. Reduced PPA time from 19.4ms → 5.3ms (3.7× faster)
- [x] **CPU byte-swap elimination** — removed the `__builtin_bswap16()` loop over 76,800 pixels per frame. PPA hardware byte-swap flag handles BE→LE conversion at zero CPU cost
- [x] **Default theme changed to "night"** (index 18) — dark theme for OLED-friendly display
- [x] **Launcher artwork wipe optimization** — eliminated redundant draws (themed header, `delete_numbers()`) under artwork that would be immediately covered
- [x] **Watchdog fix** — disabled `CONFIG_ESP_TASK_WDT_CHECK_IDLE_TASK_CPU0` in spectrum sdkconfig (USB HID Host task at priority 6 on core 0 starves IDLE task)
- [x] **Display smeared pixels fix** — original `ili9341_write_frame_rgb565()` did `memcpy(dest, src, 800×480×2)` from a 320×240 buffer (5× overread). Fixed with proper 2× scaling, then replaced entirely by PPA direct path

#### ZX Spectrum Optimization Summary Table

| Metric | Before | After | Improvement |
|--------|--------|-------|-------------|
| PPA time/frame | 19.4ms | 5.3ms | 3.7× faster |
| CPU byte-swap | ~1ms (76,800 swaps) | 0ms (HW) | Eliminated |
| Compiler opt | `-Os` | `-O2` + `-O3` (core) | Faster Z80 emu |
| **FPS** | **41.3** | **50.1** | **PAL target reached** |
| Frame budget used | 97% (PPA alone) | 27.5% | 14.4ms headroom |

> The ZX Spectrum is a PAL system (50 Hz). 50.1 FPS = full native speed.

#### Bugs fixed during PCE save/load implementation:
1. **`ResetPCE()` overwrites loaded state** → deferred load via post-gfx_init callback
2. **CPU registers aliased to struct fields** → read/write actual globals (`reg_pc_`, etc.)
3. **`gfx_init()` zeroes VDC5 after load** → one-shot callback fires after gfx_init
4. **VDC5 saved as 0 during VBLANK** → `pce_display_cr` captured at scanline `vdc_min_display`
5. **Game's VBLANK code zeros VDC5 before display** → CR protection counter (120 frames)
6. **`MY_VDC_VARS` — standalone VDC pair vars are the live registers** → save/restore all 21 standalone vars instead of dead `io.VDC[32]` array
7. **Save-order bug** — `IO_VDC_05_CR` was written to file before the `pce_display_cr` fixup → moved CR patch before standalone VDC block write

#### PCE Optimization Summary Table

| Stage | EMU | PAL | PPA | LCD | Total | FPS |
|-------|-----|-----|-----|-----|-------|-----|
| Baseline (sequential) | 14.4ms | 4.0ms | 5.4ms | 0.2ms | 24.0ms | **41.7** |
| + Async display | 18.0ms* | 5.0ms | 5.3ms | 0.2ms | 18.0ms | **55** |
| + Internal SRAM | **16.6ms** | **2.2ms** | **4.1ms** | 0.2ms | **16.6ms** | **60.0** |

*\*EMU increased due to PSRAM cache contention between cores (resolved by SRAM placement)*

### Phase 22: Atari 5200 Support & Atari 800 Display Pipeline Fix

**Goal:** Add .a52 Atari 5200 cartridge ROM support to the atari800 emulator core, and fix the display pipeline to use the correct 480×640 direct PPA path instead of the slow 800×480 framebuffer path.

**Changes:**

1. **Launcher .a52 support** (`launcher/main/main.c`):
   - Added `.a52` to `matches_rom_extension()` for step 14 (Atari 800 browser)
   - Added `.a52` → `ota_8` mapping in `get_ota_slot()`
   - Updated slot comment documentation

2. **Atari800 core .a52 handler** (`components/atari800/atari800_run.cpp`):
   - Added `.a52` extension detection in `emu_init()` → launches with `-5200 -cart-type 4 -cart` (Atari 5200 32KB standard cartridge mode)
   - Uses Altirra 5200 OS ROM replacement (built-in)

3. **Display pipeline fix** — switched from wrong 800×480 path to correct 480×640 direct PPA:
   - **Before:** `videoTask` called `ili9341_write_frame_prosystem()` which wrote palette-indexed pixels into the 800×480 `s_framebuffer` (wrong stride for 320×240 content), then `display_flush()` rotated the full 800×480 buffer
   - **After:** `videoTask` palette-converts 320×240 8-bit indexed → 320×240 RGB565 into a DMA-aligned temp buffer, then calls `ili9341_write_frame_rgb565_ex()` for the direct PPA path (2× scale + 270° rotate → 480×640, centered with 80px black bars)
   - In-game menu and volume overlay also updated to draw into the 320×240 RGB565 temp buffer and use the direct PPA path

4. **Menu deadlock fix** — volume overlay and in-game menu had nested `odroid_display_lock()` calls:
   - Code called `odroid_display_lock()`, then `ili9341_write_frame_rgb565_ex()` which internally calls `odroid_display_lock()` again → deadlock on non-recursive mutex
   - Fix: removed outer lock/unlock since `ili9341_write_frame_rgb565_ex()` manages its own locking

5. **X/Y button mapping fix** (`components/odroid/odroid_input.c`):
   - After SNES integration, physical X and Y buttons were mapped to `ODROID_INPUT_X`/`ODROID_INPUT_Y` but no longer set `ODROID_INPUT_MENU`/`ODROID_INPUT_VOLUME`
   - Fix: added OR mapping at end of `odroid_input_gamepad_read()`: `MENU |= X`, `VOLUME |= Y`
   - SNES still reads `ODROID_INPUT_X`/`ODROID_INPUT_Y` directly — no conflict

**Files modified:**
- `launcher/main/main.c` — .a52 browser filter + OTA slot mapping
- `components/atari800/atari800_run.cpp` — .a52 handler, direct PPA video, menu deadlock fix
- `components/odroid/odroid_input.c` — X→Menu, Y→Volume button mapping

**SD card ROM path:** `/sd/roms/a800/*.a52` (alongside .xex and .atr files)

---

### Phase 23: Exit Hang Fix — All Emulators

**Goal:** Fix intermittent hangs when exiting emulators back to launcher. Root cause: video/audio task cleanup used `xQueueSend(..., portMAX_DELAY)` which blocks forever if the queue is full, and polling loops had no timeout so they spin infinitely if the task never exits.

**Root Cause:** The peek-process-receive video task pattern creates a deadlock window:
1. Video task does `xQueuePeek` (success) → processes frame → `xQueueReceive` (removes item)
2. If cleanup drains the queue between peek and receive, `xQueueReceive(..., portMAX_DELAY)` blocks forever because the queue is now empty
3. Additionally, `xQueueSend(..., portMAX_DELAY)` for the stop sentinel blocks forever if the queue is already full (video task busy rendering)
4. Polling loops like `while (videoTaskIsRunning) vTaskDelay(1)` have no timeout — infinite wait if task is stuck

**Fix pattern applied to all emulators:**
1. Drain queue before sending stop sentinel (ensures space)
2. Use `xQueueOverwrite` instead of `xQueueSend` for size-1 queues (guaranteed non-blocking)
3. Add 500ms timeout to all polling loops
4. Change `xQueueReceive(..., portMAX_DELAY)` to `xQueueReceive(..., pdMS_TO_TICKS(100))` in video tasks (Stella)
5. Use separate `st_videoTaskExited` flag where cleanup sets the same flag it polls (Stella)

**Emulators fixed:**

| Emulator | File | Changes |
|----------|------|---------|
| Stella (2600) | `components/stella/stella_run.cpp` | Added `st_videoTaskExited` flag, timeout on peek/receive, drain + poll cleanup |
| gnuboy (GB) | `components/gnuboy/gnuboy_run.c` | Drain + `xQueueOverwrite` + 500ms timeout for audio & video |
| SMS Plus (SMS/GG) | `components/smsplus/smsplus_run.c` | Drain + `xQueueOverwrite` + 500ms timeout |
| ProSystem (7800) | `components/prosystem/prosystem_run.c` | Drain + `xQueueOverwrite` + 500ms timeout |
| nofrendo (NES) | `components/nofrendo/esp32/video_audio.c` | Drain + `xQueueOverwrite` + 500ms timeout |
| Spectrum (ZX) | `components/spectrum/spectrum_run.c` | Poll with 500ms timeout (was fixed 100ms delay) |
| Spectrum (single) | `components/spectrum/spmain.c` | Drain + `xQueueOverwrite` + 500ms timeout |
| Handy (Lynx) | `components/handy/handy_run.cpp` | Drain + `xQueueOverwrite` + 500ms timeout (menu + exit) |
| HuExpress (PCE) | `components/huexpress/huexpress_run.c` | Drain + `xQueueOverwrite` + 500ms timeout for audio task |

**Already safe (no changes needed):**
- Atari 800 — fixed in earlier session with `xQueueOverwrite` + timeout
- SNES — already used `xQueueOverwrite` + timeout polling

---

## 11. Bugs Fixed This Session

### Bug 1: Stack Overflow (Main Task)

**Symptom:** Random crashes/corruption in emulator apps.  
**Root Cause:** Default main task stack was only 3584 bytes. Emulator init requires much more.
**Fix:** Set `CONFIG_ESP_MAIN_TASK_STACK_SIZE=32768` in `apps/sdkconfig_common.defaults`.

### Bug 2: Store Access Fault on Core 1 (THE ROOT CAUSE)

**Symptom:** `Store access fault` crash when NES launched from OTA carousel. PPA operations also failed.
**Root Cause:** `ili9341_init()` was NEVER called in the OTA app flow.
- In the monolithic build, `main/main.c` called `ili9341_init()` directly
- In the OTA build, `app_common.c` → `odroid_system_init()` → but `odroid_system_init()` does NOT call `ili9341_init()`
- Result: `s_framebuffer = NULL` → any write to framebuffer crashes, any PPA operation needing framebuffer as destination crashes

**Fix:** Added `ili9341_init()` call in `app_init()` (in `app_common.c`) right after `odroid_system_init()`:
```c
/* 3. Hardware: I2C → PPA → USB → Audio → LCD → Touch */
odroid_system_init();

/* 3b. Allocate 320×240 virtual framebuffer + PPA output buffer */
ili9341_init();
```

Also added `#include "odroid_display.h"` to `app_common.c`.

### Bug 3: Crash Guard Cleared Too Early

**Symptom:** Crash guard counter was cleared immediately after `app_init()`, but crashes happened AFTER init (in emulator code). So the guard never accumulated 3 crashes.
**Fix:** Replaced immediate `crash_guard_clear()` with a 10-second FreeRTOS one-shot timer:
```c
TimerHandle_t guard_tmr = xTimerCreate(
    "guard_clr", pdMS_TO_TICKS(10000), pdFALSE, NULL, crash_guard_timer_cb);
if (guard_tmr) xTimerStart(guard_tmr, 0);
```
Added `#include "freertos/timers.h"` to `app_common.c`.

### Bug 4: PPA Scaling Was Wrongly Removed

**Symptom:** After NES crash investigation, PPA scaling was temporarily replaced with CPU nearest-neighbor scaling as a suspected cause. This degraded quality.
**Real Cause:** PPA scaling was fine all along — the issue was the NULL framebuffer (Bug 2 above). Once the framebuffer was properly allocated, PPA works perfectly even for fractional scales (1.25×, 1.071×, etc.).
**Fix:** Restored PPA hardware scaling for NES, GB, and SMS display functions. Confirmed smooth 60FPS graphics.

### Bug 5: ROM Browser Items Scrolling Off-Screen

**Symptom:** In the launcher ROM browser, navigating past the ~10th item caused the cursor to continue moving "off screen" — the highlighted item was drawn below the visible 480px LCD area.
**Root Cause:** `BROWSER_LIMIT` was set to 12, but with `y_start=44` (header) and `row_h=44` per row, only 10 rows fit: 44 + 10×44 = 484px ≈ 480px. Rows 11-12 were drawn at y=528-572, completely off-screen.
**Fix:** Changed `BROWSER_LIMIT` from 12 to 10 in `launcher/main/main.c`. All scroll logic (DOWN, UP, LEFT/RIGHT page, wrap-around) uses this constant, so the single change fixes all navigation paths.

---

## 12. Known Issues & Notes

1. **Game enter/exit = full reboot** — By design. OTA switching requires `esp_restart()`. Takes ~2-3s per transition. Serial log shows `rst:0xc (SW_CPU_RESET)`.

2. **SMS scrolling minor jitter** — Likely LCD vsync related, not emulator. Non-blocking queue and no frame skip were the best improvements found.

3. **ColecoVision requires BIOS** — `.col` games need `BIOS.col` (8KB) at `/sd/roms/col/BIOS.col`. Without it, `load_rom()` calls `abort()` and the crash guard rolls back to the launcher after 3 attempts. Keypad mapping is basic: Start = key 1, Select = reset.

4. **No GPIO buttons** — The Guiton board uses USB HID gamepad only. This means the safe-boot check (hold A during boot) requires USB to be enumerated first, which `app_init()` handles by polling 10× over 500ms.

5. **PSRAM for everything** — All framebuffers, ROM data, and large buffers use PSRAM (32 MB available). DMA-capable PSRAM is used for PPA input/output buffers. Exception: PCE places palette snapshot and (when available) the PPA output framebuffer in internal SRAM for cross-core cache contention avoidance.

6. **Audio sample rates** — Default init at 16000 Hz in `app_init()`. Each emulator re-initializes at its own rate (e.g., NES ~44100, SMS ~32000).

7. **Internal SRAM budget** — ESP32-P4 has ~768 KB internal SRAM total (shared with FreeRTOS heap, stacks, etc.). The PCE async display pipeline consumes up to ~154 KB for the PPA output framebuffer + 512 B palette snapshot. If internal allocation fails, it falls back to PSRAM gracefully.

---

## 13. SD Card Structure

### ROM Directories
```
/sd/roms/nes/        ← .nes files
/sd/roms/gb/         ← .gb files
/sd/roms/gbc/        ← .gbc files
/sd/roms/sms/        ← .sms files
/sd/roms/gg/         ← .gg files
/sd/roms/col/        ← .col files
/sd/roms/a78/        ← .a78 files
/sd/roms/spectrum/   ← .z80 files
/sd/roms/a26/        ← .a26 files
/sd/roms/lynx/       ← .lnx files
```

### Save Data
```
/sd/odroid/data/nes/<romname>.sav
/sd/odroid/data/gb/<romname>.sav
/sd/odroid/data/gbc/<romname>.sav
/sd/odroid/data/sms/<romname>.sav
/sd/odroid/data/gg/<romname>.sav
/sd/odroid/data/col/<romname>.sav
/sd/odroid/data/pce/<romname>.sav    ← PCE save state (v4 format, "PCES" magic)
/sd/odroid/data/spectrum/<rom>.sav
/sd/odroid/data/a78/<romname>.sav
```

### Cover Art
```
/sd/odroid/covers/<subdir>/<romname>.png
```

### System Artwork (Launcher Carousel)
```
/sd/system_art/nes.png       ← 400×225 PNG, 2× scaled to 800×450
/sd/system_art/gb.png
/sd/system_art/gbc.png
/sd/system_art/sms.png
/sd/system_art/gg.png
/sd/system_art/col.png
/sd/system_art/a78.png
/sd/system_art/spectrum.png
/sd/system_art/a26.png
/sd/system_art/lynx.png
/sd/system_art/pce.png
/sd/system_art/a800.png
/sd/system_art/favorite.png  ← Favorites system artwork
/sd/system_art/recent.png    ← Recently Played artwork
```

---

## 14. Device Recovery Procedures

### If device is stuck in a crash loop (emulator keeps rebooting):

The crash guard should catch this after 3 attempts and roll back to the launcher automatically.

### If launcher itself is broken or device won't boot:

Flash the OTA data partition to reset boot target to factory:

```powershell
Stop-Process -Name python -Force -ErrorAction SilentlyContinue
Start-Sleep -Seconds 2
python -m esptool --chip esp32p4 -p COM30 -b 460800 --before default_reset --after hard_reset write_flash --flash_mode dio --flash_size 16MB --flash_freq 80m 0xD000 firmware\ota_data_initial.bin
```

### If COM30 is busy / access denied:

```powershell
Stop-Process -Name python -Force -ErrorAction SilentlyContinue
Start-Sleep -Seconds 2
# Then retry flash command
```

### Full reflash (nuclear option):

```powershell
.\flash_all.ps1
```

---

## 15. Future Work / TODO

### Remaining Testing
- [x] Test SMS, GG, ColecoVision from carousel ✅
- [x] Test ZX Spectrum from carousel ✅
- [ ] Test Atari 2600 (stella) from carousel
- [x] Test Atari 7800 (prosystem) from carousel ✅
- [ ] Test Atari Lynx (handy) from carousel
- [x] Verify save/load works across OTA transitions for all emulators ✅

### Potential Enhancements
- [ ] Faster app switching (avoid full reboot? XIP from flash? Shared memory?)
- [x] Volume/brightness controls accessible in all emulators ✅ (Y button volume overlay)
- [x] Game state persistence across OTA switches (saves to SD — confirmed working) ✅
- [x] PC Engine emulator (huexpress) — ota_7 ✅ Ported & flashed
- [x] Atari 800 emulator (atari800) — ota_8 ✅ Ported & flashed
- [x] OpenTyrian (shoot-'em-up) — ota_9 ✅ Ported & flashed
- [ ] Better ColecoVision keypad mapping
- [ ] LCD vsync synchronization for smoother scrolling
- [ ] Apply async display pipeline technique to other emulators (currently PCE, SNES)
- [x] Atari 800 async audio pipeline — 52 → 60 FPS ✅
- [x] SNES emulator (snes9x) — ota_10 ✅ Ported, 45-67 FPS, dual-core optimized, save/load state
- [ ] AY-3-8912 PSG sound emulation for ZX Spectrum

### Source Control
- [x] GitHub repository: `https://github.com/giltal/RetroESP32-P4` — 1148 source files, merged firmware binary, .gitignore excludes build artifacts/logs/temp files

### Architecture Notes for Future Changes

- **Adding a new emulator:** Create `apps/<name>/` with CMakeLists.txt requiring `app_common` + emulator component. Add ROM extension mapping in `get_ota_slot()` in `launcher/main/main.c`. Add entry to `EMULATORS[]`, `DIRECTORIES[]`, `EXTENSIONS[]` arrays. Update `partitions_ota.csv` if needed. Add to `build_all.ps1` and `flash_all.ps1`.

- **Changing display scaling for an emulator:** Edit the corresponding function in `components/odroid/odroid_display.c`. The pattern is: convert to RGB565 in a DMA-aligned temp buffer → `ppa_rotate_scale_rgb565_to()` with scale factors → framebuffer.

- **`odroid_system_init()` vs `ili9341_init()`:** These are intentionally separate. `odroid_system_init()` initializes the physical hardware (I2C bus, PPA engine, USB, audio codec, LCD panel, touch). `ili9341_init()` allocates the virtual 320×240 framebuffer and initializes the backlight. The launcher calls them separately. Emulator apps call both via `app_init()`.

---

## Appendix: Init Sequence for OTA Emulator Apps

```
app_main()
  └─ app_init()
       ├─ nvs_flash_init()
       ├─ crash_guard_check()          ← NVS counter check/increment
       ├─ odroid_system_init()         ← I2C → PPA → USB → Audio → LCD → Touch
       ├─ ili9341_init()               ← 320×240 framebuffer + backlight
       ├─ odroid_audio_init(16000)     ← Default sample rate
       ├─ odroid_input_gamepad_init()  ← USB HID polling
       ├─ odroid_sdcard_open("/sd")    ← Mount SD card
       ├─ xTimerCreate("guard_clr")   ← 10s timer to clear crash counter
       └─ app_check_safe_boot()        ← Poll A button 10× over 500ms
  └─ app_get_rom_path(buf, len)        ← Read NVS ROM path
  └─ <emulator>_run(rom_path)          ← Blocks until exit
  └─ app_return_to_launcher()          ← Set factory partition → esp_restart()
```

---

## 16. Emulator Comparison Table

> **10 emulators + 1 launcher** — all sharing a two-stage PPA display pipeline.

### Display Pipeline Stages

**Emulators scale to 640×480 and are centered on the 480×800 LCD with 80px black bars top and bottom. They do NOT fill the full screen. Only the launcher uses the full 800×480 (480×800 after rotation) resolution.**

| Stage | What it does | Where |
|-------|-------------|-------|
| **Stage 1 — Input Scaling** | Scales native emulator resolution → 320×240 framebuffer (only needed when native res ≠ 320×240) | Per-emulator `ili9341_write_frame_*()` in `odroid_display.c` |
| **Stage 2 — Flush (most emulators)** | Copies 320×240 into 800×480 FB, then rotates 270° → 480×800 MIPI DSI LCD | `display_flush()` → `ppa_rotate_scale_rgb565_to()` in `odroid_display.c` |
| **Stage 2 — Direct (ZX Spectrum)** | PPA 2× scale + 270° rotate + byte-swap in single HW op: 320×240 → 480×640, LCD y_offset=80 | `ili9341_write_frame_rgb565_ex()` in `odroid_display.c` |
| **Stage 2 — Custom (SNES)** | PPA 2× scale + 270° rotate, no byte-swap: 256×224 → 512×448, centered on LCD | `ili9341_write_frame_rgb565_custom()` in `odroid_display.c` |

### Full Emulator Matrix

| # | Emulator | Component | Native Res | Display Function | PPA Input Scaling | PPA Flush | Frame Skip | Optimization | OTA Slot | Flash Offset | Size |
|---|----------|-----------|-----------|-----------------|:-----------------:|:---------:|:----------:|:------------:|----------|:------------:|:----:|
| — | **Launcher** | `launcher/` | 320×240 | `ili9341_write_frame_rgb565()` | No | Yes | No | `-Os` | factory | `0x10000` | 1 MB |
| 1 | **NES** (nofrendo) | `nofrendo/` | 256×224 | `ili9341_write_frame_nes()` | **Yes** | Yes | No | `-Os` | ota_0 | `0x110000` | 896 KB |
| 2 | **GB / GBC** (gnuboy) | `gnuboy/` | 160×144 | `ili9341_write_frame_gb()` | **Yes** | Yes | No | `-Os` | ota_1 | `0x1F0000` | 896 KB |
| 3 | **SMS / GG / COL** (smsplus) | `smsplus/` | 256×192 / 160×144 | `ili9341_write_frame_sms()` | **Yes** | Yes | No | `-Os` | ota_2 | `0x2D0000` | 1536 KB |
| 4 | **ZX Spectrum** | `spectrum/` | 320×240 | `ili9341_write_frame_rgb565_ex()` ⁴ | No | **Direct** ⁵ | No | **`-O2`/`-O3`** ⁶ | ota_3 | `0x450000` | 896 KB |
| 5 | **Atari 2600** (stella) | `stella/` | 320×240 | `ili9341_write_frame_rgb565()` | No | Yes | No | `-Os` | ota_4 | `0x530000` | 1536 KB |
| 6 | **Atari 7800** (prosystem) | `prosystem/` | 320×240 | `ili9341_write_frame_prosystem()` | No | Yes | **Yes** ¹ | `-Os` | ota_5 | `0x6B0000` | 896 KB |
| 7 | **Atari Lynx** (handy) | `handy/` | 160×102 | `ili9341_write_frame_lynx()` | **Yes** ² | Yes | No | `-Os` | ota_6 | `0x790000` | 896 KB |
| 8 | **PC Engine** (huexpress) | `huexpress/` | 256×240 | `ili9341_write_frame_prosystem()` | No ³ | Yes ⁷ | No | **`-O2`** ⁸ | ota_7 | `0x870000` | 1536 KB |
| 9 | **Atari 800/5200** (atari800) | `atari800/` | 320×240 | `ili9341_write_frame_rgb565_ex()` | No | **Direct** | No | **`-O2`** ⁷ | ota_8 | `0x9F0000` | 1536 KB |
| 10 | **OpenTyrian** | `opentyrian/` | 320×200 | custom PPA ⁹ | No | Yes | No | `-Os` | ota_9 | `0xBF0000` | 1536 KB |
| 11 | **SNES** (snes9x) | `snes9x/` | 256×224 | `ili9341_write_frame_rgb565_custom()` ¹¹ | No | **Custom** ¹² | **Yes** | **`-O2`/`-O3`** | ota_10 | `0xDF0000` | 2112 KB |

### Notes

1. **Atari 7800 frame skip:** `RenderFlag = frame & 1` in `prosystem_run.c` — renders every other frame to maintain performance.
2. **Atari Lynx scaling:** The Handy core renders in RAW format (per-line palette + 4-bit packed pixels). The video task decodes RAW → 160×102 RGB565, then `ili9341_write_frame_lynx()` PPA-scales 160×102 → 320×240 (sx=2.0, sy≈2.353).
3. **PC Engine scaling:** The emulator renders into a 256×240 virtual framebuffer which is palette-converted and written to the 320×240 display buffer via `ili9341_write_frame_prosystem()` (8-bit indexed → RGB565 palette lookup). No PPA at Stage 1.
4. **`ili9341_write_frame_prosystem()`** does palette conversion (8-bit indexed → RGB565) with no PPA scaling — used by Atari 7800 and PC Engine. Atari 800 previously used this path but was migrated to the direct PPA pipeline (`ili9341_write_frame_rgb565_ex()`) in Phase 22.
5. **`ili9341_write_frame_rgb565()`** — backward-compatible wrapper for standard emulators (Stella, Launcher). **`ili9341_write_frame_rgb565_ex(buf, byte_swap)`** — optimized direct PPA path used by ZX Spectrum (bypasses 800×480 framebuffer, PPA does 2× scale + rotate + byte-swap in one HW op).
6. **ZX Spectrum direct path:** PPA processes 320×240 (77K pixels) instead of 800×480 (384K pixels), reducing PPA time from 19.4ms to 5.3ms. Output is 480×640, centered on 480×800 LCD with 80px black bars.
6. **`ili9341_write_frame_lynx()`** receives decoded 160×102 RGB565, PPA-scales to 320×240 — used by Atari Lynx.
7. **All apps** compile with `CONFIG_COMPILER_OPTIMIZATION_SIZE=y` (`-Os`) from `apps/sdkconfig_common.defaults`, **except PC Engine, Atari 800, ZX Spectrum, and SNES** which override to `-O2` (Spectrum and SNES also add `-O3` for the emulation core component).
8. **PC Engine async display:** Uses a double-buffered async display pipeline — PAL+PPA+LCD runs on a dedicated core 1 task (`pce_display_task`) overlapping with emulation on core 0. Palette snapshot + framebuffer in internal SRAM to eliminate cross-core PSRAM cache contention. Achieves rock-solid 60 FPS.
9. **Atari 800 async audio:** Uses a dedicated audio task on core 1 (priority 4) with double-buffered `sampleBuffer[2]` and a queue depth of 2. POKEY generates 262 samples/frame at 15720 Hz (NTSC). Emulation runs unthrottled (`Atari800_turbo = TRUE`) with audio queue back-pressure providing implicit frame pacing. Video task also on core 1 (priority 5). Achieves 60 FPS, up from 52 FPS with blocking audio.
10. **OpenTyrian** uses custom PPA scaling via `display_set_scale(2.0f, 2.5f)` — fills the full 480×800 LCD from its 320×200 native resolution. Launched directly from carousel (no ROM browser). Game data on SD card at `/sd/tyrian/data/`.
11. **SNES display:** `ili9341_write_frame_rgb565_custom(buf, 256, 224, 2.0f, false)` — takes the native 256×224 framebuffer directly (no 320×240 staging). Green expansion (5→6 bit) is done in-place by the video task before PPA call.
12. **SNES custom path:** PPA does 2× scale + 270° rotation in one HW operation, producing 448×512 output centered on the 480×800 LCD. No byte-swap needed (snes9x already outputs LE). Audio mixing (`S9xMixSamples`) and I2S submission run entirely on Core 1 via task notification — Core 0 only runs `S9xMainLoop()` and signals the audio task.

### Flash Map (Visual)

```
0x000000 ┌──────────────────────┐
         │   Bootloader (64KB)  │
0x010000 ├──────────────────────┤
         │   Launcher (1MB)     │  factory
0x110000 ├──────────────────────┤
         │   NES (896KB)        │  ota_0
0x1F0000 ├──────────────────────┤
         │   GB/GBC (896KB)     │  ota_1
0x2D0000 ├──────────────────────┤
         │   SMS/GG/COL (1.5MB) │  ota_2
0x450000 ├──────────────────────┤
         │   ZX Spectrum (896KB)│  ota_3
0x530000 ├──────────────────────┤
         │   Atari 2600 (1.5MB) │  ota_4
0x6B0000 ├──────────────────────┤
         │   Atari 7800 (896KB) │  ota_5
0x790000 ├──────────────────────┤
         │   Atari Lynx (896KB) │  ota_6
0x870000 ├──────────────────────┤
         │   PC Engine (1.5MB)  │  ota_7
0x9F0000 ├──────────────────────┤
         │   Atari 800 (1.5MB)  │  ota_8
0xBF0000 ├──────────────────────┤
         │   OpenTyrian (1.5MB) │  ota_9
0xDF0000 ├──────────────────────┤
         │   SNES (2.1MB)       │  ota_10
0xFF0000 ├──────────────────────┤
         │   (free ~64KB)       │
0x1000000└──────────────────────┘  16MB flash
```
