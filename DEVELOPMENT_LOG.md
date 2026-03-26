# RetroESP32-P4 — Development Log & Session Continuity Guide

> **Last Updated:** March 2026 — **Phase 34: Release prep & script fixes** (removed `SDcard/` from `.gitignore` so SD card content is tracked in git; fixed `generate_merged_bin.ps1` — hashtable `+=` and em-dash encoding broke argument list in PowerShell 5.1, rewrote with parallel string arrays and `ArrayList.Add()`; regenerated merged firmware `RetroESP32_P4_v1.bin` 10.87 MB), **Phase 33: Duke3D gamepad, save/load, exit fixes** (full gamepad remapping: A=fire, B=open, X=jump, Y=crouch, L/R=weapon cycle via number key injection with held-key duration, AutoRun enabled; save/load fully working — buffer overflow fix, `access()` now probes SD for absolute paths, `numplayers` forced to 1, save name auto-fill + B button confirms; exit fix — `gameexit()` calls `_exit(0)` directly skipping heavy cleanup), **Phase 32: Duke Nukem 3D PSRAM app** (Chocolate Duke3D BUILD engine ported as fourth .papp, 320×200 8bpp→RGB565 via lcdpal[] LUT, 2.4× display scale, GRP archive from SD, multiple hang fixes: blocking getchar→return, sound precache skip, demo playback skip, menu MODE_MENU clear after skill selection, newgame() spin-wait timeout, cinematics skip; menu system working with New Game/Episode/Skill selection and in-game restart), **Phase 31: Quake audio & brightness fix** (audio task moved core 0→1, sample rate 11025→22050 Hz, mixer volume shift >>20→>>16 for proper levels, volume default 0.7→1.0, direct gamma 0.5 brightness boost in palette LUT via sqrtf curve), **Phase 30: Full rebuild & OpenTyrian cleanup** (removed OpenTyrian from `SYSTEMS[]` array, `get_ota_slot()`, `build_all.ps1`, `flash_all.ps1`, `generate_merged_bin.ps1`; fixed OTA slot mapping — SNES→ota_10, Genesis→ota_11; fixed Python env py3.11→py3.12; fixed `$ROOT` paths to `RetroESP32_P4_PSRAM`; full rebuild of launcher + 11 emulators; merged firmware binary 10.87 MB; single-shot flash at 0x0), **Phase 29: Quake PSRAM app** (WinQuake engine ported as third .papp, 320×240 software renderer, 8bpp→RGB565 native LE, 11025 Hz stereo audio, 256KB PSRAM-backed task stack via `xTaskCreateStaticPinnedToCore`, heap_caps_malloc redirect ≥1KB→PSRAM, stack overflow fix with static precache arrays, scale 2.0× for 480px LCD, gamma 0.7 for brightness, demo playback confirmed working), **Phase 28: PSRAM app stability, launcher cleanup, Atari 800 virtual keyboard** (I2S mutex deadlock fix for multi-app launches, FreeRTOS task exit crash fix, `_fstat` bug fix improving OpenTyrian load time, OpenTyrian removed from launcher carousel as standalone PSRAM app, NVS STEP bounds check prevents launcher reboot loop, Atari 800 virtual keyboard overlay with L1 toggle + Shift/Ctrl modifiers, prboom CMakeLists.txt fixed to not break other emulator builds), **Phase 27: Genesis H32 display fix, audio quality, boot logo scaling** (VDP framebuffer stride-aware conversion fixes H32 games like Rockman Mega World, audio sample rate corrected from half to full native 53 kHz, mixing improved with clamping, boot logo PNG scaled to fill 480×800 LCD), **Phase 26: Sega Genesis (Gwenesis) emulator port** (M68K+Z80+VDP+YM2612+SN76489, ROM bounds checking for SVP carts, internal RAM optimization ~10% FPS gain, X/Y button mapping, sidebar labels, launcher integration), **Phase 25: SNES sidebar buttons & input fix** (visual MENU/VOL touch-zone labels in SNES side bars, direct DPI framebuffer writes bypassing DMA2D contention, X/Y gamepad buttons restored to native SNES mapping), **Phase 24: Display pipeline & menu rendering fix** (all Pipeline A emulators now use direct PPA 2× + 270° path via `s_emu_scaled` 320×240 buffer, in-game menus draw into emu buffer not 800×480 framebuffer), **Phase 23: Exit hang fix** (all emulators), **Atari 5200 support** (.a52 extension, 5200 cart mode, direct PPA 480×640 display pipeline, X/Y button fix), **SNES save/load state** (full emulator snapshot to SD card, menu integration), **SNES DKC crash fix** (NO_ZERO_LUT — COLOR_SUB1_2 NULL dereference), SNES (snes9x) integration & optimization (42→50 FPS, dual-core audio offload, direct 2× PPA scaling, DSP tuning), ZX Spectrum full optimization (PPA direct 320×240→480→640 pipeline, Kempston joystick, -O3, 41→50 FPS), launcher native 800×480 UI overhaul (PNG artwork, VGA font, icon fixes), PCE save/load state (v4 format), Atari 800 async audio (52→60 FPS), PCE 60 FPS optimization, ZX Spectrum crash fixes, Atari 7800 exit fix, PPA S→R→M fix, OpenTyrian integration, in-game menus for all emulators, launcher browser fixes.
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

**Emulators Ported (11 total + 1 game):**

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
| 9 | Atari 800 / 5200 | atari800 | ota_8 | .xex, .atr, .a52 | ✅ Confirmed **60FPS** (async audio, 5200 cart, **virtual keyboard via L1**) |
| 10 | OpenTyrian | opentyrian | ~~ota_9~~ PSRAM app | .tyr | ✅ Working (now a PSRAM app, removed from launcher carousel) |
| 11 | **SNES** | snes9x | ota_10 | .smc, .sfc | ✅ Working, **45-67 FPS** (dual-core, 2× PPA, DKC crash fixed, save/load state) |
| 12 | **Sega Genesis** | gwenesis | ota_11 | .md, .gen | ✅ Working, ~30 FPS (frameskip=2, internal RAM optimized, ROM bounds checking) |
| 13 | **Quake** | WinQuake | PSRAM app | .papp | ✅ Working (320×240, 8MB hunk, 256KB PSRAM stack, demo playback confirmed) |
| 14 | **Duke Nukem 3D** | Chocolate Duke3D | PSRAM app | .papp | ✅ Working (320×200, BUILD engine, GRP archive, menu + in-game restart working) |

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
| factory | app | factory | 0x10000 | 704 KB | Launcher (620 KB) |
| ota_0 | app | ota_0 | 0xC0000 | 640 KB | NES/nofrendo (563 KB) |
| ota_1 | app | ota_1 | 0x160000 | 640 KB | GB-GBC/gnuboy (535 KB) |
| ota_2 | app | ota_2 | 0x200000 | 1.31 MB | SMS-GG-COL/smsplus (1227 KB) |
| ota_3 | app | ota_3 | 0x350000 | 768 KB | ZX Spectrum (661 KB) |
| ota_4 | app | ota_4 | 0x410000 | 1.25 MB | Atari 2600/stella (1180 KB) |
| ota_5 | app | ota_5 | 0x550000 | 640 KB | Atari 7800/prosystem (571 KB) |
| ota_6 | app | ota_6 | 0x5F0000 | 640 KB | Atari Lynx/handy (531 KB) |
| ota_7 | app | ota_7 | 0x690000 | 640 KB | PC Engine/huexpress (571 KB) |
| ota_8 | app | ota_8 | 0x730000 | 832 KB | Atari 800-5200/atari800 (728 KB) |
| ota_9 | app | ota_9 | 0x800000 | 768 KB | OpenTyrian (662 KB) |
| ota_10 | app | ota_10 | 0x8C0000 | 960 KB | SNES/snes9x (864 KB) |
| ota_11 | app | ota_11 | 0x9B0000 | 1.31 MB | Sega Genesis/gwenesis (1260 KB) |

### OTA Slot ↔ Extension Mapping (`get_ota_slot()` in launcher)

```
Slot 0  (ota_0):  .nes            → nofrendo
Slot 1  (ota_1):  .gb, .gbc       → gnuboy
Slot 2  (ota_2):  .sms, .gg, .col → smsplus
Slot 3  (ota_3):  .z80, .sna      → spectrum
Slot 4  (ota_4):  .a26, .bin      → stella
Slot 5  (ota_5):  .a78            → prosystem
Slot 6  (ota_6):  .lnx            → handy
Slot 7  (ota_7):  .pce            → huexpress
Slot 8  (ota_8):  .xex, .atr, .a52 → atari800
Slot 9  (ota_9):  (OpenTyrian — launched directly, no ROM extension)
Slot 10 (ota_10): .smc, .sfc      → snes9x
Slot 11 (ota_11): .md, .gen       → gwenesis
```

---

## 6. Display Pipeline (PPA)

> **CRITICAL RULE — DO NOT MIX UP THE TWO PIPELINES:**
> - **Launcher ONLY** uses `s_framebuffer` (800×480) + `display_flush()` / `display_flush_force()`
> - **ALL emulators** use `s_emu_scaled` (320×240) + `display_emu_flush_320x240()` (internal) or `display_emu_flush()` (public API for menus)
> - **NEVER use `FB_W` (800) or `FB_H` (480) in emulator display code** — always use `EMU_W` (320) and `EMU_H` (240)
> - **NEVER use `display_get_framebuffer()` or `display_flush_force()` from emulator code** — these return/push the 800×480 launcher buffer
> - In-game menus must draw into `display_get_emu_buffer()` (320×240) and call `display_emu_flush()` — NOT `display_get_framebuffer()` + `display_flush_force()`

The display uses PPA hardware acceleration with two pipelines. **Phase 24 unified all emulators onto the fast direct path.**

### Emulator Pipeline (all emulators — direct PPA 2× + rotate)

All emulators render to a shared 320×240 RGB565 buffer (`s_emu_scaled`). For non-native resolutions, PPA hardware scaling is used as Stage 1 to reach 320×240. Then `display_emu_flush_320x240()` does Stage 2: PPA 2× scale + 270° rotation in a single hardware operation → 480×640 → LCD centered with 80px black bars.

**Stage 1 — Scale native → 320×240:**

| Emulator | Native Res | Scale X | Scale Y | Method |
|----------|-----------|---------|---------|--------|
| NES | 256×224 | 1.25× | 1.071× | PPA scale → `s_emu_scaled` |
| Game Boy | 160×144 | 2.0× | 1.667× | PPA scale → `s_emu_scaled` |
| Game Gear | 160×144 | 2.0× | 1.667× | PPA scale → `s_emu_scaled` |
| SMS | 256×192 | 1.25× | 1.25× | PPA scale → `s_emu_scaled` |
| ColecoVision | 256×192 | 1.25× | 1.25× | PPA scale (via SMS path) → `s_emu_scaled` |
| Atari 7800 | 320×240 | 1.0× | 1.0× | Palette convert → `s_emu_scaled` (native match) |
| Atari 2600 | 320×240 | 1.0× | 1.0× | Direct RGB565 → `s_emu_scaled` |
| Atari Lynx | 160×102 | 2.0× | 2.353× | PPA scale → `s_emu_scaled` |
| C64 | 384×272 | crop | crop | CPU crop center → `s_emu_scaled` |
| ZX Spectrum | 320×240 | — | — | Direct (native match, byte-swap in Stage 2) |
| SNES | 256×224 | — | — | Own path (direct 2× PPA, doesn't use `s_emu_scaled`) |
| Atari 800 | 336×240 | — | — | Own async path (crop+scale, doesn't use `s_emu_scaled`) |
| OpenTyrian | 320×200 | — | — | Uses 800×480 launcher path (not a console emulator) |

**Stage 2 — `display_emu_flush_320x240()` (shared by all Pipeline A emulators):**

```
s_emu_scaled (320×240 RGB565, DMA-aligned, PSRAM)
    │
    ▼ PPA: scale 2× + rotate 270° + optional byte-swap (single HW op)
    │
s_ppa_out_buf (480×640 RGB565)
    │
    ▼ st7701_lcd_draw_rgb_bitmap(0, 80, 480, 640)
    │
Physical LCD (480×800, 80px black bars top+bottom)
```

**Performance:** PPA processes only 320×240 = 77K pixels (not 800×480 = 384K). PPA time ~5.3ms per frame.

### In-Game Menu Rendering (all emulators)

> **CRITICAL:** Menus use helper functions (`draw_char`, `fill_rect`) that draw with a **hardcoded 320-pixel stride** (`fb[yy * 320 + xx]`). They MUST draw into a 320-wide buffer.

Correct pattern for in-game menus and volume overlays:
```c
uint16_t *fb = display_get_emu_buffer();   // returns s_emu_scaled (320×240)
// ... draw menu using fb with 320-pixel stride ...
display_emu_flush();                        // PPA 2× + rotate → LCD
```

**WRONG (causes twisted/repeated menu):**
```c
uint16_t *fb = display_get_framebuffer();  // WRONG: returns s_framebuffer (800×480)
// draw with 320-stride into 800-wide buffer → rows misaligned → garbage
display_flush_force();                      // WRONG: pushes 800×480 → PPA rotate → LCD
```

### Launcher Pipeline (native 800×480)

The launcher writes directly to an 800×480 framebuffer (`s_framebuffer`) at native resolution. PPA does rotation only (no scaling). Uses `display_get_framebuffer()` + `display_flush()`. See Phase 17.

OpenTyrian also uses this path since it renders at 320×200 but uses the launcher's 800×480 infrastructure.

### Buffer Allocations

| Buffer | Size | Location | Purpose |
|--------|------|----------|---------|
| `s_framebuffer` | 768,000 bytes | PSRAM+DMA, 64-byte aligned | 800×480 RGB565 — **LAUNCHER ONLY** |
| `s_emu_scaled` | 153,600 bytes | PSRAM+DMA, 64-byte aligned | 320×240 RGB565 — **ALL EMULATORS** (shared) |
| `s_ppa_out_buf` | 768,000 bytes | PSRAM+DMA, 64-byte aligned | 480×800 max rotated+scaled output |
| `s_nes_temp` | 114,688 bytes | PSRAM+DMA, 64-byte aligned | NES 256×224 RGB565 temp (Stage 1 input) |
| `s_gb_temp` | 46,080 bytes | PSRAM+DMA, 64-byte aligned | GB 160×144 RGB565 temp (Stage 1 input) |
| `s_sms_temp` | 98,304 bytes | PSRAM+DMA, 64-byte aligned | SMS 256×192 RGB565 temp (Stage 1 input) |
| `s_lynx_temp` | 32,640 bytes | PSRAM+DMA, 64-byte aligned | Lynx 160×102 RGB565 temp (Stage 1 input) |

### Key Constants (odroid_display.c)

| Constant | Value | Used By |
|----------|-------|---------|
| `FB_W` | 800 | Launcher framebuffer width — **NEVER use in emulator code** |
| `FB_H` | 480 | Launcher framebuffer height — **NEVER use in emulator code** |
| `EMU_W` | 320 | Emulator display width — use this in all emulator display functions |
| `EMU_H` | 240 | Emulator display height — use this in all emulator display functions |
| `EMU_PIXELS` | 76,800 | `EMU_W × EMU_H` — use for loop counts in emulator pixel processing |

### Public API Summary

| Function | Returns/Does | Used By |
|----------|-------------|--------|
| `display_get_framebuffer()` | `s_framebuffer` (800×480) | Launcher, OpenTyrian ONLY |
| `display_flush()` / `display_flush_force()` | Push 800×480 → PPA rotate → LCD | Launcher, OpenTyrian ONLY |
| `display_get_emu_buffer()` | `s_emu_scaled` (320×240) | Emulator in-game menus |
| `display_emu_flush()` | Push 320×240 → PPA 2×+rotate → LCD | Emulator in-game menus |
| `ili9341_write_frame_*()` | Emulator-specific render (internal) | Called by each emulator's video task |

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
- [x] Atari 800 virtual keyboard overlay (5×13 grid, L1 toggle, Shift/Ctrl sticky modifiers)
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

### Phase 24: Display Pipeline & Menu Rendering Fix

**Goal:** Fix broken display scaling and corrupted in-game menus across NES, GB/GBC, SMS/GG/ColecoVision, Atari 7800, PCE, and Lynx.

**Symptoms reported:**
- NES, GB, GBC, SMS, GG, ColecoVision: Resizing to full screen 800×480 instead of 640×480
- Atari 7800, PCE: Total garbage on screen
- In-game menus: Appeared "multiple times twisted" when pressing X
- Atari 2600, Lynx, Atari 800, SNES, OpenTyrian: Unaffected (used separate display paths)

**Root Cause 1 — Display scaling used wrong constants:**
All Pipeline A emulator display functions (`ili9341_write_frame_gb/nes/sms/prosystem/lynx/c64`) used `FB_W` (800) and `FB_H` (480) for scale factor calculations and output buffer addressing. These constants are for the **launcher's** 800×480 framebuffer. Emulators should target 320×240.

- **Scale factors were wrong:** e.g., NES computed `sx = FB_W/256 = 3.125×` instead of `320/256 = 1.25×`
- **Output buffer overflow:** ProSystem & PCE looped `FB_PIXELS/4 = 96,000` iterations instead of `EMU_PIXELS/4 = 19,200` — reading past the end of the input buffer → garbage pixels
- **PPA pushed 800×480 to LCD** instead of the fast 320×240 direct path

**Root Cause 2 — In-game menus drew into wrong buffer:**
Menu helper functions (`draw_char`, `fill_rect`) use hardcoded 320-pixel stride: `fb[yy * 320 + xx]`. But `display_get_framebuffer()` returns the 800-wide `s_framebuffer`. Writing 320-stride rows into an 800-wide buffer creates wrong row offsets → twisted/repeated rendering. Then `display_flush_force()` pushes the full 800×480 through PPA rotate, making the corruption worse.

**Fix 1 — Unified emulator display pipeline (`odroid_display.c`):**
- Added `EMU_W=320`, `EMU_H=240`, `EMU_PIXELS`, `EMU_SIZE` constants
- Added `s_emu_scaled` — shared 320×240 DMA-aligned buffer for all emulators
- Added `display_emu_flush_320x240()` — internal helper: PPA 2× scale + 270° rotate → 480×640 → LCD centered with 80px bars
- Rewrote all 6 Pipeline A display functions to: scale native → `s_emu_scaled` (320×240) → `display_emu_flush_320x240()`
- Added public APIs: `display_get_emu_buffer()` (returns `s_emu_scaled`) and `display_emu_flush()` (calls the 320x240 flush helper)

**Fix 2 — In-game menu rendering (5 emulators):**

| Emulator | File | Change |
|----------|------|--------|
| NES | `components/nofrendo/esp32/video_audio.c` | `display_get_framebuffer()` → `display_get_emu_buffer()`, `display_flush_force()` → `display_emu_flush()` |
| GB/GBC | `components/gnuboy/gnuboy_run.c` | Same pattern (volume + menu) |
| SMS/GG/COL | `components/smsplus/smsplus_run.c` | Same pattern |
| Atari 7800 | `components/prosystem/prosystem_run.c` | Same pattern |
| PCE | `components/huexpress/huexpress_run.c` | Same pattern |

**Lesson learned — Rules to prevent recurrence:**
1. **Never use `FB_W`/`FB_H` in emulator display code** — these are 800/480 for the launcher only
2. **Never use `display_get_framebuffer()` from emulators** — it returns the 800×480 launcher buffer
3. **Emulator menus must use `display_get_emu_buffer()` + `display_emu_flush()`** — this gives the correct 320-wide buffer matching the hardcoded stride
4. **Loop counts processing emulator pixels must use `EMU_PIXELS`** (76,800) not `FB_PIXELS` (384,000)
5. **All emulator display functions output to `s_emu_scaled` and call `display_emu_flush_320x240()`** — never write to `s_framebuffer`

**Files changed (7):** `odroid_display.c`, `odroid_display.h`, `video_audio.c`, `gnuboy_run.c`, `smsplus_run.c`, `prosystem_run.c`, `huexpress_run.c`

**Apps rebuilt & flashed:** NES, GB, SMS, ProSystem, PCE, Spectrum, Handy

### Phase 25: SNES Sidebar Buttons & Input Fix

**Goal:** Add visible MENU and VOL labels to the SNES emulator’s black side bars so users can see where to touch, and restore X/Y gamepad buttons to their native SNES function.

**Context:** SNES outputs 256×224, scaled 2× + 270° rotated to 448×512 on the 480×800 portrait LCD. This leaves two black side bars: portrait y=0…143 (landscape left, 144px) and y=656…799 (landscape right, 144px). The touch screen maps these zones to MENU and VOLUME.

**Implementation — Sidebar button rendering:**
- Pre-rendered two button bitmaps (80×100 MENU, 80×84 VOL) with a 5×5 bitmap font at 3× scale
- Buttons have dark gray background (`0x18E3`), border (`0x6B4D`), white text
- Font rendering accounts for 270° rotation: font columns map to increasing portrait Y, font rows map to decreasing portrait X (so text reads L→R in landscape)
- Persistent PSRAM buffers allocated once with `heap_caps_aligned_calloc(64, …, MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA)`

**Bug — DMA2D semaphore contention (only one button rendered):**
- **Symptom:** Only the first sidebar button appeared; the second was invisible regardless of position
- **Root cause:** The ESP-IDF DPI panel’s `draw_bitmap` with DMA2D uses `xSemaphoreTake(draw_sem, 0)` (non-blocking). When the game frame’s async DMA2D transfer is still in-flight, the next `draw_bitmap` call returns `ESP_ERR_INVALID_STATE` silently. The first button sometimes succeeded (DMA completed between calls), but the second always failed.
- **Fix:** Added `st7701_lcd_draw_to_fb()` — writes directly to the DPI framebuffer via CPU memcpy + `esp_cache_msync()`, completely bypassing the async DMA2D pipeline. No semaphore contention possible.

**Performance — Draw once, not every frame:**
- The game frame only covers portrait (16,144)–(463,655); sidebar regions are never overwritten after the initial border clear
- Sidebar buttons are blitted only on the first 2 video frames (covers the `s_custom_borders_cleared` one-time clear), then never again — zero per-frame overhead

**Input fix — X/Y gamepad buttons:**
- **Problem:** `odroid_input_gamepad_read()` globally mapped X→MENU and Y→VOLUME for all emulators. This was correct for emulators without native X/Y (NES, GB, etc.) but wrong for SNES which needs X/Y as face buttons.
- **Fix:** Added `odroid_input_xy_menu_disable` global flag. SNES sets it `true` on init. When set, the input driver skips the X→Menu/Y→Volume aliasing. Touch screen MENU/VOLUME still works normally.

**Files changed:**
| File | Change |
|------|--------|
| `components/st7701_lcd/st7701_lcd.c` | Added `st7701_lcd_draw_to_fb()` — direct DPI framebuffer write |
| `components/st7701_lcd/include/st7701_lcd.h` | Declaration for `st7701_lcd_draw_to_fb()` |
| `components/st7701_lcd/CMakeLists.txt` | Added `esp_mm` to PRIV_REQUIRES (for `esp_cache_msync`) |
| `components/odroid/odroid_input.c` | Added `odroid_input_xy_menu_disable` flag, conditional X/Y→Menu/Vol |
| `components/odroid/include/odroid_input.h` | Declaration for `odroid_input_xy_menu_disable` |
| `apps/snes/main/snes_run.c` | Sidebar init/blit system, draw-once logic, sets xy_menu_disable |
| `apps/snes/main/CMakeLists.txt` | Added `st7701_lcd` to REQUIRES |

### Phase 26: Sega Genesis (Gwenesis) Emulator Port

**Goal:** Port the Gwenesis Sega Genesis/Mega Drive emulator core to ESP32-P4 as ota_11, with full M68K + Z80 + VDP + YM2612 FM + SN76489 PSG emulation.

**Emulation Core — Gwenesis:**
- M68K CPU (main processor) + Z80 CPU (sound co-processor)
- VDP (Video Display Processor) — 320×224 native resolution, 8-bit indexed framebuffer with 64-color CRAM palette
- YM2612 FM synthesis (6-channel) + SN76489 PSG (4-channel square/noise)
- Audio at 26634 Hz, frameskip=2 for performance

**Display Pipeline:**
- Genesis VDP renders 320×224 @ 8-bit indexed color into `gwenesis_vdp_get_framebuffer()`
- Palette conversion: 8-bit indexed → RGB565 using CRAM palette (inline in render loop)
- PPA 2× scale + 270° rotate: 320×224 → 448×640 on 480×800 LCD
- Side bars: 80px each (portrait y=0…79 and y=721…799), MENU/VOL touch buttons at py=2/ph=76 and py=722/ph=76
- Uses `ili9341_write_frame_rgb565_custom(buf, 320, 224, 2.0f, false)` — same path as SNES

**Build & Integration:**
- Component at `components/gwenesis/` with M68K, Z80, VDP, bus, sound sub-modules
- App at `apps/genesis/` — `genesis_run.c` is the main emulation wrapper (~1100 lines)
- Launcher: single "SEGA GENESIS" entry in carousel, handles both `.md` and `.gen` extensions via `matches_rom_extension()` in `get_ota_slot()` step 17
- OTA slot 11, flash offset 0x9B0000, size 1.31 MB (binary 1260 KB)

**Key Fixes During Porting:**

1. **VRAM allocation collision:** Original code had both `unsigned char VRAM[0x10000]` in `gwenesis_vdp.c` and `unsigned char VRAM[0x10000]` as extern in `gwenesis_bus.c` — two separate 64 KB arrays. Resolved by making VDP's VRAM the canonical one and aliasing the bus reference.

2. **`sizeof(OPNREGS)` — struct vs array mismatch:** `OPNREGS` was declared as `unsigned char OPNREGS[0x200]` (array, 512 bytes) but code used `sizeof(OPNREGS)` expecting struct size. Fixed by using explicit `0x200` constant.

3. **LSB_FIRST endianness:** Gwenesis core required `#define LSB_FIRST` for correct byte ordering on the RISC-V ESP32-P4 (little-endian).

4. **ROM bounds checking (Virtua Racing crash fix):** Games with the SVP chip (Virtua Racing) access ROM addresses beyond the 2 MB ROM buffer, causing `Load access fault` (MEPC=0x4005b338, MTVAL=0x5d789918). Added `gwenesis_rom_size` global variable set in `load_cartridge()`, and bounds-checked all FETCH macros in `m68k.h`:
   ```c
   extern unsigned int gwenesis_rom_size;
   #define FETCH8ROM(A) (((unsigned int)((A)^1) < gwenesis_rom_size) ? ROM_DATA[((A)^1)] : 0xFF)
   #define FETCH16ROM(A) (((unsigned int)(A) < gwenesis_rom_size) ? *(unsigned short*)&ROM_DATA[(A)] : 0xFFFF)
   #define FETCH32ROM(A) (((unsigned int)(A)+2 < gwenesis_rom_size) ? ... : 0xFFFFFFFF)
   ```

5. **M68K address error emulation disabled:** Turned off `M68K_EMULATE_ADDRESS_ERROR` in `m68kconf.h` — removes setjmp/longjmp overhead per M68K instruction, measurable performance gain.

**Internal RAM Optimization (~10% FPS improvement):**
All hot emulation data moved from PSRAM to internal SRAM via `heap_caps_calloc(…, MALLOC_CAP_INTERNAL)`:
- M68K CPU state (`m68ki_cpu` struct)
- M68K RAM (64 KB) — with PSRAM fallback if internal allocation fails
- Z80 RAM (ZRAM, 8 KB)
- VRAM (64 KB) — with PSRAM fallback
- CRAM palette (128 bytes), VSRAM (80 bytes)
- VDP registers, render line buffers, sprite buffers
- YM2612 FM synth state (OPNREGS 512B, sin_tab, tl_tab)
- Audio mixing buffers (SN76489 + YM2612)

**Input Mapping:**
- X/Y buttons mapped to Genesis emulator (A button) — `odroid_input_xy_menu_disable = true`
- D-pad → Genesis D-pad, A → B button, B → C button, Start → Start
- Touch screen MENU/VOL zones still work normally

**Launcher Integration:**
- Single "SEGA GENESIS" entry (merged from separate .md/.gen entries)
- `definitions.h` COUNT reduced 19→18
- `matches_rom_extension()` handles both `.md` and `.gen` at slot 17 → ota_11
- SD card paths: ROMs at `/sd/roms/gen/`, saves at `/sd/odroid/data/genesis/`, artwork at `/sd/system_art/gen.png`

**Files changed:**
| File | Change |
|------|--------|
| `apps/genesis/main/genesis_run.c` | Complete emulation wrapper — display, audio, input, internal RAM allocations |
| `components/gwenesis/src/cpus/M68K/m68k.h` | Bounds-checked FETCH8ROM/FETCH16ROM/FETCH32ROM macros |
| `components/gwenesis/src/bus/gwenesis_bus.c` | `gwenesis_rom_size` global, set in `load_cartridge()` |
| `components/gwenesis/src/cpus/M68K/m68kconf.h` | `M68K_EMULATE_ADDRESS_ERROR` → OFF |
| `launcher/main/includes/definitions.h` | COUNT 19→18 |
| `launcher/main/main.c` | Merged Genesis entries, `.md`+`.gen` extension handling |
| `launcher/main/includes/structures.h` | Single Genesis entry in SYSTEMS[] |
| `partitions_ota.csv` | Added ota_11 at 0x9B0000/0x150000 for Genesis |

### Phase 32: Duke Nukem 3D (Chocolate Duke3D) PSRAM App
- [x] Ported Chocolate Duke Nukem 3D (BUILD engine) as fourth .papp
- [x] 320×200 8bpp indexed → RGB565 conversion via lcdpal[] LUT, 2.4× display scale
- [x] GRP archive loading from `/sd/roms/duke3d/duke3d.grp`
- [x] Fixed blocking `getchar()` → `return` in error handlers (filesystem.c, tiles.c)
- [x] Fixed sound precache hang — skipped `precachenecessarysounds()` (450+ SD seeks)
- [x] Fixed demo playback hang — `foundemo = 0` to skip demos
- [x] Fixed menu restart hang — MODE_MENU cleared after skill selection in menues.c case 110
- [x] Fixed `newgame()` infinite spin-wait — added 3000-iteration timeout on `Sound[].lock`
- [x] Skipped cinematics (`ud.showcinematics = 0`)
- [x] `access()` syscall stub returns -1 (avoids slow SD card probing)
- [x] Menu system working: New Game → Episode → Skill selection, in-game restart via Start

### Phase 33: Duke3D Gamepad Mapping, Save/Load Fix, Exit Fix
- [x] Full gamepad remapping: A→fire(LCTRL), B→open(Space), X→jump(A key), Y→crouch(Z key)
- [x] L/R weapon cycling via number key injection (KEYDOWN on press, KEYUP on release)
- [x] AutoRun enabled by default (`ud.auto_run = 1`)
- [x] Fixed save/load "version mismatch" — `fullpathsavefilename[16]` buffer overflow (path needs 26 bytes), increased to 512
- [x] Fixed save path separator — backslash `\` → forward slash `/` for POSIX/SD card
- [x] Fixed exit resetting console — `gameexit()` now calls `_exit(0)` directly, skipping heavy engine/audio cleanup
- [x] Fixed `access()` stub — now probes SD card for absolute paths (save files), returns -1 only for relative paths (GRP sounds)
- [x] Fixed `numplayers` — forced to 1 before load comparison (`.data` initializer unreliable in PSRAM XIP)
- [x] Save name auto-fill — empty slots get default "SAVE N" name
- [x] B button (Space) confirms save name entry — `strget()` accepts Space alongside Enter
- [x] Save/load game fully working — saves to `/sd/roms/duke3d/game0.sav` etc.

### Phase 34: Release Prep & Script Fixes
- [x] Removed `SDcard/` from `.gitignore` — SD card folder now tracked in git for distribution
- [x] Fixed `generate_merged_bin.ps1` — hashtable-based flash map broke in PowerShell 5.1 (property access via `+=` on arrays, em-dash character encoding). Rewrote with parallel string arrays, `[ArrayList].Add()`, and plain ASCII
- [x] Replaced hardcoded `$idfPython` path with `python` (available after IDF export)
- [x] Regenerated merged firmware `RetroESP32_P4_v1.bin` (10.87 MB, all 15 binaries)

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
- [x] Sega Genesis emulator (gwenesis) — ota_11 ✅ Ported, ~30 FPS, internal RAM optimized, ROM bounds checking
- [ ] AY-3-8912 PSG sound emulation for ZX Spectrum
- [x] Atari 800 virtual keyboard (L1 toggle, 5×13 grid, Shift+Ctrl modifiers) ✅
- [x] OpenTyrian moved to PSRAM app, removed from launcher carousel ✅
- [x] Launcher NVS STEP bounds check (prevents reboot loop after emulator removal) ✅
- [x] prboom CMakeLists.txt fixed (empty component stub, doesn't break other emulator builds) ✅
- [x] Quake (WinQuake) ported as PSRAM app — 320×240 software renderer, demo playback working ✅
- [x] PSRAM-backed large-stack tasks in psram_app_loader (>32KB via `xTaskCreateStaticPinnedToCore`) ✅
- [x] Duke Nukem 3D (Chocolate Duke3D) ported as PSRAM app — BUILD engine, 320×200, menu system working ✅

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

> **11 emulators + 1 launcher** — all sharing a two-stage PPA display pipeline.

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
| — | **Launcher** | `launcher/` | 320×240 | `ili9341_write_frame_rgb565()` | No | Yes | No | `-Os` | factory | `0x10000` | 704 KB |
| 1 | **NES** (nofrendo) | `nofrendo/` | 256×224 | `ili9341_write_frame_nes()` | **Yes** | Yes | No | `-Os` | ota_0 | `0x0C0000` | 640 KB |
| 2 | **GB / GBC** (gnuboy) | `gnuboy/` | 160×144 | `ili9341_write_frame_gb()` | **Yes** | Yes | No | `-Os` | ota_1 | `0x160000` | 640 KB |
| 3 | **SMS / GG / COL** (smsplus) | `smsplus/` | 256×192 / 160×144 | `ili9341_write_frame_sms()` | **Yes** | Yes | No | `-Os` | ota_2 | `0x200000` | 1.31 MB |
| 4 | **ZX Spectrum** | `spectrum/` | 320×240 | `ili9341_write_frame_rgb565_ex()` ⁴ | No | **Direct** ⁵ | No | **`-O2`/`-O3`** ⁶ | ota_3 | `0x350000` | 768 KB |
| 5 | **Atari 2600** (stella) | `stella/` | 320×240 | `ili9341_write_frame_rgb565()` | No | Yes | No | `-Os` | ota_4 | `0x410000` | 1.25 MB |
| 6 | **Atari 7800** (prosystem) | `prosystem/` | 320×240 | `ili9341_write_frame_prosystem()` | No | Yes | **Yes** ¹ | `-Os` | ota_5 | `0x550000` | 640 KB |
| 7 | **Atari Lynx** (handy) | `handy/` | 160×102 | `ili9341_write_frame_lynx()` | **Yes** ² | Yes | No | `-Os` | ota_6 | `0x5F0000` | 640 KB |
| 8 | **PC Engine** (huexpress) | `huexpress/` | 256×240 | `ili9341_write_frame_prosystem()` | No ³ | Yes ⁷ | No | **`-O2`** ⁸ | ota_7 | `0x690000` | 640 KB |
| 9 | **Atari 800/5200** (atari800) | `atari800/` | 320×240 | `ili9341_write_frame_rgb565_ex()` | No | **Direct** | No | **`-O2`** ⁷ | ota_8 | `0x730000` | 832 KB |
| 10 | **OpenTyrian** | `opentyrian/` | 320×200 | custom PPA ⁹ | No | Yes | No | `-Os` | ota_9 | `0x800000` | 768 KB |
| 11 | **SNES** (snes9x) | `snes9x/` | 256×224 | `ili9341_write_frame_rgb565_custom()` ¹¹ | No | **Custom** ¹² | **Yes** | **`-O2`/`-O3`** | ota_10 | `0x8C0000` | 960 KB |
| 12 | **Sega Genesis** (gwenesis) | `genesis/` | 320×224 | `ili9341_write_frame_rgb565_custom()` ¹³ | No | **Custom** | **Yes** ¹⁴ | `-Os` | ota_11 | `0x9B0000` | 1.31 MB |

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
13. **Genesis display:** `ili9341_write_frame_rgb565_custom(buf, 320, 224, 2.0f, false)` — same custom PPA path as SNES. Palette conversion (8-bit indexed → RGB565 via CRAM) done inline in render loop before PPA call. 80px side bars with MENU/VOL touch buttons.
14. **Genesis frame skip:** `frameskip=2` — renders every 3rd frame. Audio runs every frame for smooth sound. Internal RAM optimization moves all hot data (M68K state, RAM, VRAM, CRAM, YM2612 synth tables) to internal SRAM for ~10% FPS improvement.

### Flash Map (Visual)

```
0x000000 ┌──────────────────────┐
         │   Bootloader (64KB)  │
0x010000 ├──────────────────────┤
         │   Launcher (704KB)   │  factory
0x0C0000 ├──────────────────────┤
         │   NES (640KB)        │  ota_0
0x160000 ├──────────────────────┤
         │   GB/GBC (640KB)     │  ota_1
0x200000 ├──────────────────────┤
         │   SMS/GG/COL (1.3MB) │  ota_2
0x350000 ├──────────────────────┤
         │   ZX Spectrum (768KB)│  ota_3
0x410000 ├──────────────────────┤
         │   Atari 2600 (1.25MB)│  ota_4
0x550000 ├──────────────────────┤
         │   Atari 7800 (640KB) │  ota_5
0x5F0000 ├──────────────────────┤
         │   Atari Lynx (640KB) │  ota_6
0x690000 ├──────────────────────┤
         │   PC Engine (640KB)  │  ota_7
0x730000 ├──────────────────────┤
         │   Atari 800 (832KB)  │  ota_8
0x800000 ├──────────────────────┤
         │   OpenTyrian (768KB) │  ota_9
0x8C0000 ├──────────────────────┤
         │   SNES (960KB)       │  ota_10
0x9B0000 ├──────────────────────┤
         │   Genesis (1.31MB)   │  ota_11
0xB00000 ├──────────────────────┤
         │   (free ~5MB)        │
0x1000000└──────────────────────┘  16MB flash
```

### Phase 27: Genesis H32 Display Fix, Audio Quality & Boot Logo Scaling

**Fix 1 — Genesis H32 mode display (Rockman Mega World):**

**Problem:** Games running in H32 mode (256-pixel width, e.g., Rockman Mega World) had a completely garbled/skewed display.

**Root cause:** The VDP framebuffer always uses a fixed 320-byte stride (`screen_buffer[line * 320]`), even when only 256 pixels per line are valid in H32 mode. The `gen_convert_frame()` function read w×h bytes contiguously (256×224 = 57,344 bytes), treating the data as having a 256-byte stride. This caused every scanline after the first to be offset by 64 bytes (320-256), producing a diagonal skew across the entire frame.

**Fix:** Rewrote `gen_convert_frame()` to be stride-aware — iterates per-row using a fixed 320-byte source stride and outputs to a packed w-wide destination. The 4-pixel-at-a-time PSRAM optimization is preserved within each row.

```c
static void gen_convert_frame(const uint8_t *src, uint16_t *dst,
                              const uint16_t *palette, int w, int h)
{
    const int stride = 320;  /* VDP framebuffer stride is always 320 */
    for (int y = 0; y < h; y++) {
        const uint8_t *row = src + y * stride;
        uint16_t *out = dst + y * w;
        /* ... 4-pixel optimized loop per row ... */
    }
}
```

**File changed:** `apps/genesis/main/genesis_run.c` — `gen_convert_frame()`

---

**Fix 2 — Genesis audio sample rate & mixing quality:**

**Problem:** Genesis audio was unclear/distorted.

**Root cause (sample rate):** The I2S output was initialized at half the native rate (`GWENESIS_AUDIO_FREQ_NTSC / 2` = 26634 Hz). The Gwenesis core generates ~888 samples per frame. At 26634 Hz, 888 samples take 33ms to play back — double the 16.67ms NTSC frame time. This caused audio buffer backup and timing-related distortion.

**Root cause (mixing):** The SN76489 + YM2612 mixer used `(sn + ym) >> 1` which halves the dynamic range, producing muffled/quiet audio. It also doesn't handle overflow correctly.

**Fix:** 
1. Changed audio init from `GWENESIS_AUDIO_FREQ_NTSC / 2` to `GWENESIS_AUDIO_FREQ_NTSC` (53267 Hz) — now 888 samples play in exactly one frame period (16.67ms)
2. Replaced `>> 1` mixing with additive mixing + int16 clamping: `sum = sn + ym; clamp to [-32768, 32767]` — fuller volume, no overflow artifacts

**File changed:** `apps/genesis/main/genesis_run.c` — `AUDIO_SAMPLE_RATE`, `odroid_audio_init()` call, audio task mixing loop

---

**Fix 3 — Boot logo PNG scaling:**

**Change:** Updated `show_png_logo_native()` in the launcher to scale the boot logo PNG to fill the full 480×800 LCD instead of displaying at 1:1 native size. Uses independent X/Y scale factors computed from LCD dimensions divided by rotated image dimensions, allowing non-uniform stretch to fill the screen entirely.

**File changed:** `launcher/main/main.c` — `show_png_logo_native()`

---

### Phase 28: PSRAM App Stability, Launcher Cleanup & Atari 800 Virtual Keyboard

**Fix 1 — I2S mutex deadlock on multi-app launch (PSRAM apps):**

**Problem:** Launching a second PSRAM app after the first caused the system to hang indefinitely — stuck at "Loading..." screen.

**Root cause:** `vTaskDelete()` on the audio task while it was blocked inside `i2s_channel_write()` permanently locked the I2S mutex. The next app's `i2s_channel_enable()` would deadlock trying to acquire the same mutex.

**Fix:** Graceful audio task exit pattern — tasks check `papp_exit_requested` flag, set a `done` flag, then spin in `while(1) delay_ms(100)` (never return, preventing FreeRTOS `prvTaskExitError`). Added `audio_reset_sample_rate()` API (sets cached rate to 0) so the next app's `i2s_channel_disable` is skipped when no valid rate is cached.

**Files changed:** `components/audio/audio.c`, `apps/psram_doom/papp_i_sound.c`, `apps/psram_opentyrian/papp_sdl_audio.c`

---

**Fix 2 — `_fstat` missing file size (both PSRAM apps):**

**Problem:** OpenTyrian took ~45 seconds to load (should be ~3s). Doom save game load crashed with `Z_Malloc: 1341265604 bytes`.

**Root cause:** The `_fstat()` syscall shim only set `st_mode` but left `st_size` uninitialized (stack garbage). Newlib's `fseek(SEEK_END)` + `ftell()` returned garbage values.

**Fix:** `memset(st, 0, sizeof(*st))` + compute real file size via seek-to-end-and-back in both `_fstat()` and `_stat()`.

**Files changed:** `apps/psram_doom/papp_syscalls.c`, `apps/psram_opentyrian/papp_syscalls.c`

---

**Fix 3 — OpenTyrian removed from launcher carousel:**

**Change:** OpenTyrian is now exclusively a PSRAM app (loaded from SD card). Removed from the launcher's `EMULATORS[]`, `DIRECTORIES[]`, `EXTENSIONS[]` arrays, `COUNT` decreased from 19→18, all `STEP == 15` special cases removed, SNES/Genesis/PSRAM Apps step indices shifted down.

**Files changed:** `launcher/main/main.c`, `launcher/main/includes/definitions.h`

---

**Fix 4 — Launcher reboot loop after emulator removal:**

**Problem:** After removing OpenTyrian (COUNT 19→18), the launcher kept resetting in an infinite loop.

**Root cause:** NVS stored `STEP=18` (old PSRAM Apps index). With COUNT=18, valid range is 0-17, so `STEP=18` caused out-of-bounds array access on every carousel reference (`EMULATORS[STEP]`, `EXTENSIONS[STEP]`, etc.), crashing and rebooting.

**Fix:** Added bounds clamp after NVS read: `if (STEP < 0 || STEP >= COUNT) STEP = 0;`

**File changed:** `launcher/main/main.c` — `get_step_state()`

---

**Feature — Atari 800 virtual keyboard overlay:**

**Trigger:** L1 button toggles keyboard on/off.

**Layout:** 5×13 grid covering the full Atari 800 keyboard:
- Row 0: Numbers 1-0, `<`, `>`
- Row 1: Q-P, `-`, `+`, `=`
- Row 2: A-L, `;`, `*`, Return (RT), Delete (DL)
- Row 3: Z-M, `,`, `.`, `/`, Escape (ES), Shift (SH), Ctrl (CT)
- Row 4: Space bar, Backspace (BS), Tab (TB), Help (HL), Option (OP), Start (ST), Select (SE)

**Controls:** D-pad navigates, A presses selected key, B closes keyboard. Shift and Ctrl are sticky toggles (green highlight when active, status bar shows modifier state).

**Rendering:** Drawn as RGB565 overlay on the `s_vid_rgb565` framebuffer in `videoTask` after palette conversion, before LCD write. Uses the existing `a800_draw_string()`/`a800_fill_rect()` helpers. Joystick input suppressed while keyboard is visible.

**Key injection:** Sets `INPUT_key_code` to the appropriate `AKEY_*` value with `AKEY_SHFT`/`AKEY_CTRL` modifiers applied.

**File changed:** `components/atari800/atari800_run.cpp`

---

**Fix 5 — prboom CMakeLists.txt breaking other emulator builds:**

**Problem:** After copying the Doom engine source into `components/prboom/`, building any emulator app failed with "Failed to resolve component 'retro-go'" because `prboom/CMakeLists.txt` had `COMPONENT_REQUIRES "retro-go"`.

**Fix:** Replaced with `idf_component_register()` (empty stub). The prboom source is only compiled by `tools/build_doom_papp.ps1`, not as a regular ESP-IDF component.

**File changed:** `components/prboom/CMakeLists.txt`

---

### Phase 29: Quake (WinQuake) PSRAM App

**Goal:** Port the WinQuake engine (from quake-go / ESP32 Quake project) as the third PSRAM XIP app (.papp).

**Engine:** WinQuake — original Quake software renderer, 320×240, 8-bit indexed color with palette, 11025 Hz stereo audio.

**Source:** 207 files copied to `components/quake/`, with `CMakeLists.txt` set to empty stub (same pattern as prboom — source compiled only by the build script, not as an ESP-IDF component).

**Shim files created** in `apps/psram_quake/`:

| File | Purpose |
|------|---------|
| `papp_main.c` | Entry point, 256KB game task on PSRAM stack, watchdog, cleanup |
| `papp_syscalls.c` | Newlib stubs (8192-entry alloc tracker, 48-slot FD table), redirects ≥1KB to PSRAM |
| `papp_vid.c` | Video: 320×240 8bpp→RGB565 (native LE, no byte-swap), scale 2.0×, double-buffered surfaces, 640KB surfcache |
| `papp_snd.c` | Audio: 256 SFX slots, 512-sample buffer, 11025 Hz stereo, `audio_submit` |
| `papp_input.c` | Gamepad→Quake `Key_Event` mapping (A=fire, B=jump, L/R=strafe, etc.) |
| `papp_sys.c` | Sys_Printf via `log_printf`, file I/O via service table, gamma 0.7, 8MB hunk |
| `papp_rg_stubs.c` | ~40 retro-go API stubs (Quake-go used retro-go framework) |

**Compat headers** in `apps/psram_quake/compat/`: 15 ESP-IDF/FreeRTOS stub headers (same pattern as Doom).

---

**Fix 1 — Pack info allocation failure:**

**Problem:** `heap_caps_malloc` in Quake's `common.c` requested `MALLOC_CAP_INTERNAL` for 21696 bytes (pack file info). Internal SRAM too small.

**Fix:** `papp_syscalls.c` redirects all `heap_caps_malloc` calls ≥1KB to PSRAM regardless of requested caps.

---

**Fix 2 — Stack overflow during demo playback:**

**Problem:** `CL_ParseServerInfo()` allocates `model_precache[256][64]` + `sound_precache[256][64]` = 32KB on the stack. The default 32KB task stack was immediately exhausted during `demo1.dem` playback.

**Fix (two parts):**
1. Made `model_precache` and `sound_precache` arrays `static` in `cl_parse.c`
2. Created game task with 256KB stack — but `xTaskCreatePinnedToCore` can't allocate 256KB from internal SRAM

**File changed:** `components/quake/winquake/cl_parse.c`

---

**Fix 3 — PSRAM-backed large-stack task creation (launcher infrastructure):**

**Problem:** `xTaskCreatePinnedToCore` fails when requesting 256KB — FreeRTOS allocates task stacks from internal SRAM by default, which doesn't have 256KB free.

**Fix:** Modified `svc_task_create()` in `psram_app_loader.c`: for stack requests >32KB, allocates stack buffer from PSRAM via `heap_caps_malloc(MALLOC_CAP_SPIRAM)`, TCB from internal RAM via `heap_caps_malloc(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT)`, then uses `xTaskCreateStaticPinnedToCore`. For requests ≤32KB, uses the standard `xTaskCreatePinnedToCore`.

**File changed:** `components/psram_app_loader/psram_app_loader.c`

---

**Fix 4 — Black screen (scale overflow):**

**Problem:** Initial scale factor 2.5× produced 600px wide output on 480px LCD width. PPA hardware silently failed.

**Fix:** Changed scale to 2.0× → 480×640 output fits within 480×800 panel.

**File changed:** `apps/psram_quake/papp_vid.c`

---

**Fix 5 — Wrong colors (palette byte-swap):**

**Problem:** Original quake-go swaps RGB565 bytes for retro-go's `rg_display_submit` (big-endian format). The PSRAM app uses `display_write_frame_custom` which expects native little-endian RGB565.

**Fix:** Removed byte-swap from palette LUT construction in `VID_SetPalette`.

**File changed:** `apps/psram_quake/papp_vid.c`

---

**Fix 6 — Dark display:**

**Problem:** Quake default gamma=1.0 too dark on small LCD panel.

**Fix:** Forced `Cvar_SetValue("gamma", 0.7)` after init (lower value = brighter in Quake).

**File changed:** `apps/psram_quake/papp_sys.c`

---

**Build:** `tools/build_quake_papp.ps1` — compiles 71 objects (64 WinQuake engine + 7 shim files), ~350KB .papp binary, ~544KB BSS.

**Upload:** `python tools/upload_papp.py firmware/quake.papp --port COM30`

**Requires:** `id1/pak0.pak` (Quake shareware/full) at `/sd/roms/quake/id1/pak0.pak` on SD card.

**Result:** Quake running smoothly — demo playback confirmed with full game messages ("You got the rockets", "You receive 25 health"), correct colors, good brightness.

### Phase 30: Full Rebuild & OpenTyrian Cleanup

**Goal:** After removing OpenTyrian from the launcher carousel (Phase 28), residual references in multiple files caused SNES and Genesis to break. Full cleanup, rebuild, and reflash of all firmware.

**Bug 1 — SNES pops back to launcher immediately:**

**Root cause:** `get_ota_slot()` mapped `.smc`/`.sfc` → slot 9 (ota_9 at 0x800000, the old OpenTyrian partition). But the SNES binary was flashed at ota_10 (0x8C0000). Booting from an empty/stale ota_9 slot triggered the crash guard → automatic return to launcher.

**Fix:** Changed `get_ota_slot()` to return 10 for SNES and 11 for Genesis, matching the actual partition table.

**Bug 2 — Genesis shows OpenTyrian icon, screen is black:**

**Root cause (icon):** `SYSTEMS[]` array in `structures.h` still had the OpenTyrian entry at index 15, pushing SNES to index 16 and Genesis to index 17. With `COUNT=18`, Genesis at [17] displayed correctly but used the wrong icon sprite (`&tyrian` at [15] instead of `&genesis`).

**Root cause (black screen):** `get_ota_slot()` mapped `.md`/`.gen` → slot 10 (ota_10 at 0x8C0000), which contained the SNES binary. Genesis booked into the SNES emulator which couldn't parse a Genesis ROM → black screen.

**Fix:** Removed OpenTyrian entry from `SYSTEMS[]` array.

**Bug 3 — Build scripts had stale references:**

- `build_all.ps1`, `flash_all.ps1`, `generate_merged_bin.ps1` all still included `opentyrian_app.bin`
- `$ROOT` pointed to old `RetroESP32_P4` instead of `RetroESP32_P4_PSRAM`
- Python env path referenced broken `py3.11` env (esptool dependency mismatch); working env is `py3.12`

**Fix:** Removed OpenTyrian from all three scripts, updated `$ROOT` and Python env paths.

**Full rebuild:** Built launcher + 11 emulators (NES, GB, SMS, Spectrum, Stella, ProSystem, Handy, PCE, Atari800, SNES, Genesis). Generated merged binary `RetroESP32_P4_v1.bin` (10.87 MB). Flashed in one shot at address 0x0.

**Files changed:**

| File | Change |
|------|--------|
| `launcher/main/includes/structures.h` | Removed OpenTyrian entry from `SYSTEMS[]` array |
| `launcher/main/main.c` | Fixed `get_ota_slot()`: SNES→10, Genesis→11 (was 9, 10) |
| `build_all.ps1` | Removed OpenTyrian, fixed `$ROOT`, updated Python env to py3.12 |
| `flash_all.ps1` | Removed OpenTyrian, fixed `$ROOT`, updated Python env to py3.12 |
| `generate_merged_bin.ps1` | Removed OpenTyrian, fixed `$ROOT`, updated Python env to py3.12 |

**Result:** All 11 emulators confirmed working — SNES runs games, Genesis has correct icon and displays properly.

### Phase 31: Quake Audio & Brightness Fix

**Goal:** Fix Quake PSRAM app having no sound and dark display.

**Bug 1 — No audio output:**

**Root cause (3 issues):**
1. **Audio task on wrong core** — `task_create(..., 0)` pinned to core 0 (game core). Both game and audio fighting for CPU on the same core, with audio starved. Doom and OpenTyrian both use core 1.
2. **Sample rate too low** — `audio_init(11025)` was half the standard rate. Doom uses 22050 Hz. The I2S driver timing mismatch caused buffer issues.
3. **Mixer volume attenuated 16× too much** — output scaling used `>> 20` (divide by 1M) instead of `>> 16` (divide by 65536). With `master_vol` (0-255) × `sample` (±32767) × `volumeInt` (256), the correct shift to get back to 16-bit range is 16, not 20.

**Fix:**
- Moved audio task to core 1: `task_create(..., 1)`
- Increased sample rate: `audio_init(22050)` + updated WAV resampling step to match
- Fixed mixer shift: `>> 20` → `>> 16`
- Raised volume default from 0.7 to 1.0

**Bug 2 — Display too dark:**

**Root cause:** WinQuake's `v_gamma` cvar (range 0.5-1.0, lower=brighter) controls brightness via `BuildGammaTable()` → `V_UpdatePalette()` → `VID_ShiftPalette()`. Setting the cvar via `Cvar_SetValue("gamma", 0.5)` after `Host_Init` wasn't taking effect — the engine's `V_UpdatePalette` early-returns when no color shifts change and gamma hasn't changed since last check.

**Fix:** Applied gamma 0.5 brightness boost directly in the palette→RGB565 LUT construction in `VID_Update()`. Each palette entry goes through `sqrtf(c/255) * 255` (gamma 0.5 = square root curve), which significantly brightens dark areas while preserving whites. This bypasses the engine's cvar chain entirely and always works.

**Files changed:**

| File | Change |
|------|--------|
| `apps/psram_quake/papp_snd.c` | Audio task core 0→1, sample rate 11025→22050 Hz, mixer `>>20`→`>>16`, volume 0.7→1.0 |
| `apps/psram_quake/papp_vid.c` | Direct gamma 0.5 sqrtf brightness boost in palette LUT, added `<math.h>` |
| `apps/psram_quake/papp_sys.c` | Gamma cvar set to 0.5 (belt-and-suspenders with direct LUT boost) |

**Result:** Quake has clear, loud audio and bright, visible display on the small LCD.

### Phase 32: Duke Nukem 3D (Chocolate Duke3D) PSRAM App

**Goal:** Port Chocolate Duke Nukem 3D (BUILD engine) to ESP32-P4 as the fourth PSRAM XIP app (.papp).

**Engine:** Chocolate Duke3D — a source port of Duke Nukem 3D based on the BUILD engine. Renders 320×200 in 8-bit indexed color. Uses GRP archive format for game data (tiles, sounds, maps, CON scripts).

**Display Pipeline:**
- BUILD engine renders 320×200 @ 8bpp indexed into SDL framebuffer
- `SDL_Flip()` shim converts 8bpp → RGB565 via `lcdpal[]` lookup table (256 entries, built from `SDL_SetColors()`)
- Output via `display_write_frame_custom(buf, 320, 200, 2.4f, false)` — 2.4× scale fills 768×480 on the 480×800 LCD
- Implemented in `apps/psram_duke3d/papp_sdl_video.c`

**Input Pipeline:**
- `papp_sdl_event.c` maps USB HID gamepad → SDL events → BUILD engine scancodes
- D-pad → arrow keys, A → LCTRL (fire), B → SPACE (open), Start → ESCAPE (menu)
- Scancodes feed into `KB_KeyDown[]` array used by the BUILD engine

**Hang Fixes (6 issues resolved):**

| # | Symptom | Root Cause | Fix |
|---|---------|-----------|-----|
| 1 | Hang on tile/GRP error | `getchar(); exit(0)` blocks forever on embedded | Replaced with `return` in filesystem.c, tiles.c |
| 2 | Hang during level load | `precachenecessarysounds()` does 450+ SD card `access()` calls | Skipped in `cacheit()` |
| 3 | Hang after tile caching | Demo playback starts complex map, `displayrooms()` extremely slow on PSRAM | Set `foundemo = 0` to skip demos |
| 4 | Hang on menu restart | `menues.c` case 110 (skill select) calls `newgame()`+`enterlevel()` but never clears MODE_MENU — next frame re-enters menu, re-triggers `newgame()` | Added `gm &= ~MODE_MENU; KB_FlushKeyboardQueue(); return;` after enterlevel |
| 5 | Infinite spin-wait | `newgame()` has `while(Sound[globalskillsound].lock>=200)` that never exits | Added 3000-iteration timeout |
| 6 | Slow startup | Cinematics spin-wait on `totalclock` timers | `ud.showcinematics = 0` |

**Syscall Stubs (papp_syscalls.c):**
- `access()` → returns -1 always (avoids SD card probing for GRP sounds)
- `_open()` → returns -1 for relative paths
- `Z_AvailHeap()` → returns 8 MB
- `getch()` → returns 'y'

**Build:**
- Component at `components/duke3d/` with Engine/ (BUILD renderer, filesystem, tiles) and Game/ (game logic, menus, premap) and Audiolib/ sub-modules
- App at `apps/psram_duke3d/` — SDL shim layer (video, events, audio, syscalls)
- Build script: `tools/build_duke3d_papp.ps1` — compiles 43 objects, links ~660 KB .papp
- Game data: ATOMIC 1.5 `duke3d.grp` at `/sd/roms/duke3d/duke3d.grp`

**Files changed:**

| File | Change |
|------|--------|
| `components/duke3d/Game/game.c` | Cinematics skip, demo skip, auto-start bypass removed (menu restored), diagnostic prints |
| `components/duke3d/Game/menues.c` | Case 110: clear MODE_MENU + flush keyboard after enterlevel |
| `components/duke3d/Game/premap.c` | `newgame()` spin-wait timeout, sound precache skip in `cacheit()` |
| `components/duke3d/Engine/filesystem.c` | Removed blocking `getchar()` in error handlers |
| `components/duke3d/Engine/tiles.c` | Fixed escaped `'\\n'` → `'\n'`, removed blocking `getchar()` |
| `apps/psram_duke3d/papp_syscalls.c` | `access()` returns -1, `getch()` returns 'y' |
| `apps/psram_duke3d/papp_sdl_video.c` | 8bpp→RGB565 via lcdpal[] LUT, 2.4× scale |
| `apps/psram_duke3d/papp_sdl_event.c` | Gamepad→SDL→BUILD scancode mapping |
| `apps/psram_duke3d/papp_main.c` | PSRAM app entry, 256KB stack task on core 0 |

**Result:** Duke Nukem 3D boots, shows main menu, allows New Game → Episode → Skill selection, loads E1L1 with music, game loop runs at ~3.3 FPS. In-game menu restart (Start → New Game) works correctly.

### Phase 33: Duke3D Gamepad Mapping, Save/Load Fix, Exit Fix

**Goal:** Make Duke3D fully playable with proper gamepad controls, fix broken save/load, and fix exit crashing the console.

**Gamepad Remapping:**

Original mapping had X→E key and R→D key, neither bound to any Duke3D action. Remapped for full playability:

| Button | Key | Action |
|--------|-----|--------|
| D-pad | Arrow keys | Movement |
| A | LCTRL | Fire |
| B | Space | Open/Use |
| X | A key | Jump |
| Y | Z key | Crouch |
| L | Number keys (cycle down) | Previous weapon |
| R | Number keys (cycle up) | Next weapon |
| Start | Escape | Menu |
| Select | Tab | Automap |

**Weapon Cycling Deep Dive:**
`gamefunc_Next_Weapon` / `gamefunc_Previous_Weapon` are dead code in Chocolate Duke3D — bits 8-11 in `sync.bits` are packed but never extracted by `processinput()`. Solved by injecting actual number key events (1-9, 0 for slot 10). Key must be *held* for at least one game frame — initial implementation queued KEYDOWN+KEYUP in same poll cycle, engine never saw the press. Final fix: KEYDOWN on shoulder button press, KEYUP on shoulder button release, with `s_l_held_key`/`s_r_held_key` tracking.

AutoRun enabled by default: `ud.auto_run = 1` in `config.c`.

**Save/Load "Version Mismatch" Fix:**

`saveplayer()` in `menues.c` declared `fullpathsavefilename[16]` — only 16 bytes. With `game_dir = "/sd/roms/duke3d"`, the full path `"/sd/roms/duke3d/game0.sav"` needs 26 bytes. This **stack buffer overflow** corrupted the adjacent `bv` (BYTEVERSION=119) variable, causing `loadplayer()` to read a wrong version number and reject the save with "OLD VERSION" (FTA quote 114).

Also fixed path separator from `\\` (Windows backslash, from original PC port) to `/` for POSIX/SD card filesystem.

**Exit Crash Fix:**

`Error()` in `global.c` called `exit()` which goes through newlib's C library shutdown — stdio buffer flushing, `atexit()` handlers, lock acquisition. In the PSRAM app context, these internal operations touch invalid state or acquire locks that don't exist, causing a crash that resets the console. Changed to `_exit()` which directly calls `app_return_to_launcher()` → `longjmp` back to the entry point. All resource cleanup (tasks, sound, file handles, heap) is handled by `papp_main.c`'s `app_entry()` cleanup sequence anyway.

**Save File Not Found on Load:**

`access()` was stubbed to always return -1 (to avoid 450+ slow SD probes for GRP sounds). But `SafeFileExists()` uses `access()`, and `TCkopen4load()` uses `SafeFileExists()` to decide whether to prepend `game_dir`. With `access()` always failing, the full path `/sd/roms/duke3d/game0.sav` was never tried — it fell back to bare `game0.sav` which `_open()` rejects (relative path). Fixed `access()` to probe SD card for absolute paths while still returning -1 for relative paths (GRP items).

**numplayers=0 on Load:**

`numplayers` is initialized to 1 in `dummy_multi.c` (.data section), but the `.data` initializer may not survive PSRAM XIP loading reliably. Forced `numplayers = 1` before the comparison in `loadplayer()`.

**No Enter Key for Save Confirmation:**

Duke3D's `strget()` only accepted Enter (ASCII 13) to confirm save name entry. No gamepad button mapped to Enter. Fixed by also accepting Space (B button) as confirmation. Auto-fill empty slots with "SAVE N" default names so no keyboard is needed.

**gameexit() Crash:**

`gameexit()` ran heavy cleanup: `SoundShutdown()` → `MV_Shutdown()` → `SDL_DestroyMutex()`, `uninitengine()`, `CONFIG_WriteSetup()`, `uninitgroupfile()`. On embedded, this crashes due to shared audio state being accessed during teardown. Replaced entire body with `_exit(0)` — PSRAM app cleanup in `papp_main.c` handles everything.

**Files changed:**

| File | Change |
|------|--------|
| `apps/psram_duke3d/papp_sdl_event.c` | Full gamepad remapping, weapon cycling via number key injection (KEYDOWN/KEYUP on press/release) |
| `apps/psram_duke3d/papp_syscalls.c` | `access()` probes SD for absolute paths; `_open()` I/O tracing for writes |
| `components/duke3d/Game/config.c` | `ud.auto_run = 1` for AutoRun by default |
| `components/duke3d/Game/game.c` | `gameexit()` → direct `_exit(0)`, `strget()` accepts Space as Enter |
| `components/duke3d/Game/menues.c` | Save buffer `[16]`→`[512]`, `\\`→`/`, auto-fill save names, loadpheader diagnostics |
| `components/duke3d/Game/global.c` | `Error()`: `exit()`→`_exit()`, added `<unistd.h>` |

**Result:** Duke3D fully playable with all essential actions mapped. Save/load game fully working (saves to `/sd/roms/duke3d/gameN.sav`). Exiting game returns cleanly to launcher without console reset.

### Phase 34: Release Prep & Script Fixes

**Goal:** Prepare repository for distribution — track SD card content in git, fix broken merge script.

**`.gitignore` update:**
- Removed `SDcard/` entry so SD card folder (ROMs, system art) is tracked in git for distribution.

**`generate_merged_bin.ps1` rewrite:**

The script was broken in PowerShell 5.1 due to two issues:
1. **Hashtable array `+=`** — `$merge_args += $entry.Offset` appends the hashtable object reference, not the string value, when used inside a `foreach` loop with `@()` arrays.
2. **Em-dash character (`—`)** — UTF-8 encoded em-dashes in comments/strings caused PowerShell 5.1 (which reads scripts as ANSI by default) to misparse the file, corrupting nearby string literals.

**Fix:** Rewrote the flash map using three parallel string arrays (`$offsets`, `$files`, `$descs`) with a `for` loop, `[System.Collections.ArrayList].Add()` for reliable argument building, plain ASCII throughout, and `python` instead of hardcoded `$idfPython` path.

**Files changed:**

| File | Change |
|------|--------|
| `.gitignore` | Removed `SDcard/` entry |
| `generate_merged_bin.ps1` | Full rewrite: hashtable → parallel arrays, `+=` → `ArrayList.Add()`, em-dash → ASCII, `$idfPython` → `python` |

**Result:** `generate_merged_bin.ps1` runs correctly in PowerShell 5.1, generates `RetroESP32_P4_v1.bin` (10.87 MB) with all 15 binaries.
