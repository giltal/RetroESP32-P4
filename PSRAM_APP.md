# PSRAM App Execution — Architecture & Implementation

> **Status:** ✅ Fully implemented and working.  
> **Apps:** OpenTyrian, Doom (prboom-go), Quake (WinQuake) — load from SD, execute in PSRAM, return to launcher on exit.

---

## Overview

The RetroESP32-P4 loads and executes game/emulator binaries from the SD card directly into PSRAM at runtime. This eliminates the need for dedicated OTA flash partitions and full reboots.

**How it works:**
1. `.papp` files stored on SD card at `/sd/roms/papp/`
2. Launcher reads the binary into PSRAM
3. MMU maps the PSRAM region as executable (XIP — eXecute In Place)
4. App runs as a function call — on exit, launcher continues immediately (no reboot)

**Benefits:**
- No flash wear from repeated OTA writes
- Near-instant app switching (< 1 second load from SD)
- Unlimited apps — just drop `.papp` files on the SD card
- 32 MB PSRAM available for app code + data + heap

---

## System Architecture

```
┌─────────────────────────────────────────────────────┐
│  LAUNCHER (factory partition, always in flash)       │
│                                                      │
│  ┌────────────────────────────────────────────────┐  │
│  │ psram_app_loader                               │  │
│  │  - psram_app_load(path)  → load .papp from SD  │  │
│  │  - psram_app_run(handle) → MMU map + call       │  │
│  │  - psram_app_unload()    → free + unmap         │  │
│  └────────────────────────────────────────────────┘  │
│                                                      │
│  ┌────────────────────────────────────────────────┐  │
│  │ app_services_t  (60+ function pointers)        │  │
│  │  display_*, audio_*, input_*, file_*,          │  │
│  │  mem_*, settings_*, task_*, log_printf, ...    │  │
│  └────────────────────────────────────────────────┘  │
└────────────────┬─────────────────────────────────────┘
                 │ entry_fn(&services)
                 ▼
┌─────────────────────────────────────────────────────┐
│  LOADED APP (in PSRAM, executed via cache/MMU)       │
│                                                      │
│  int app_entry(const app_services_t *svc) {          │
│      // All ESP-IDF calls go through svc->*          │
│      // malloc/free redirected via --wrap            │
│      // File I/O via svc->file_open/read/write       │
│      return 0;  // back to launcher, no reboot!      │
│  }                                                   │
└─────────────────────────────────────────────────────┘
```

---

## .papp Binary Format

A `.papp` file is a flat binary with a 32-byte header:

```
Offset  Size  Field        Description
0x00    4     magic        0x50415050 ("PAPP")
0x04    4     version      ABI version (currently 1)
0x08    4     entry_off    Byte offset to entry function from text start
0x0C    4     text_size    .text + .rodata size in bytes
0x10    4     data_size    .data (initialized) size in bytes
0x14    4     bss_size     .bss (zero-fill) size in bytes
0x18    4     flags        Reserved (0)
0x1C    4     reserved     Padding (0)
───────────────────────────────────────────
0x20    N     payload      .text + .rodata + .data (flat binary)
```

**Created with:** `python tools/pack_papp.py input.bin output.papp --bss-size N`

---

## Load → Run → Unload Lifecycle

### Launcher Flow (main.c)

```
1. serial_upload_deinit()          ← Stop USB JTAG driver (prevents stalls)
2. esp_log_level_set("*", NONE)   ← Suppress logging during load
3. psram_app_load(path, &handle)   ← Read .papp from SD into PSRAM
4. psram_app_run(handle)           ← MMU map → call entry → wait → unmap
5. esp_log_level_set("*", INFO)    ← Restore logging
6. psram_app_unload(handle)        ← Free PSRAM buffer
7. serial_upload_init()            ← Restart USB JTAG listener
8. draw_carousel_screen()          ← Restore launcher UI
```

### psram_app_load() Detail

1. Open `.papp` file, read & validate 32-byte header
2. Allocate **page-aligned PSRAM buffer** (64 KB alignment via `heap_caps_aligned_alloc`)
3. `fread()` entire binary (text + rodata + data) into buffer
4. `memset()` the BSS region to zero
5. **Cache writeback:** `esp_cache_msync(buf, C2M | DATA)` — push data cache to physical PSRAM

### psram_app_run() Detail

1. Get physical address: `esp_mmu_vaddr_to_paddr(buf) → paddr`
2. Create executable MMU mapping: `esp_mmu_map(paddr, size, EXEC|READ|32BIT, PADDR_SHARED) → exec_ptr`
3. **Cache invalidate:** `esp_cache_msync(exec_ptr, M2C | INST)` — force CPU to fetch fresh instructions
4. Populate service table with launcher function pointers
5. Call `entry_fn(&services)` — blocks until app returns
6. Unmap: `esp_mmu_unmap(exec_ptr)`

### Key Constraints

- **MMU capabilities:** Only `EXEC | READ | 32BIT` allowed (no WRITE or 8BIT per hardware `s_mem_caps_check()`)
- **MMU page size:** 64 KB (0x10000)
- **PADDR_SHARED:** I-bus and D-bus see the same PSRAM virtual range — loader writes via D-bus, CPU fetches via I-bus
- **Cache coherence:** Must writeback D-cache **before** invalidating I-cache

---

## Memory Layout (ESP32-P4, 32 MB PSRAM)

```
PSRAM Virtual Address Map:
┌─────────────────────────────────┐ 0x48000000
│  Launcher heap / framebuffers   │  (~2 MB, existing allocations)
│  s_framebuffer, s_ppa_out_buf   │
├ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─┤
│  Free PSRAM heap                │
├─────────────────────────────────┤ 0x4A000000  ← Linker base / exec mapping
│  Loaded app .text + .rodata     │  (~314 KB OpenTyrian / ~529 KB Doom / ~350 KB Quake)
│  Loaded app .data               │
│  Loaded app .bss                │  (~629 KB OpenTyrian / ~64 KB Doom / ~544 KB Quake)
│  App heap (malloc → PSRAM)      │
├─────────────────────────────────┤
│  Remaining free PSRAM           │  (~30 MB available)
└─────────────────────────────────┘ 0x4A000000 + 32MB
```

---

## Building PSRAM Apps

### OpenTyrian

```powershell
# Source ESP-IDF toolchain
& "C:\Users\97254\esp\v5.5.2\esp-idf\export.ps1"

# Build (compiles 58 files, links, objcopy, packs .papp automatically)
.\tools\build_opentyrian_papp.ps1

# Upload to device via USB Serial JTAG
python tools\upload_papp.py firmware\opentyrian.papp --port COM30
```

### Doom (prboom-go)

```powershell
# Source ESP-IDF toolchain
& "C:\Users\97254\esp\v5.5.2\esp-idf\export.ps1"

# Build (compiles 76 files: 70 prboom engine + 6 shims)
.\tools\build_doom_papp.ps1

# Upload to device
python tools\upload_papp.py firmware\doom.papp --port COM30
```

**Doom requires** a Doom IWAD file (`doom.wad`) at `/sd/roms/doom/doom.wad` on the SD card.

### Quake (WinQuake)

```powershell
# Source ESP-IDF toolchain
& "C:\Users\97254\esp\v5.5.2\esp-idf\export.ps1"

# Build (compiles 71 files: 64 WinQuake engine + 7 shims)
.\tools\build_quake_papp.ps1 -Clean

# Upload to device
python tools\upload_papp.py firmware\quake.papp --port COM30
```

**Quake requires** game data at `/sd/roms/quake/id1/pak0.pak` on the SD card (shareware or full).

**Additional compiler flags** (beyond the common set):

| Flag | Purpose |
|------|----------|
| `-DESP32_QUAKE=1` | Enables ESP32-specific code paths |
| `-DESP_PLATFORM=1` | Provides `MALLOC_CAP_*` defines |
| `-DCONFIG_IDF_TARGET_ESP32P4=1` | Target identification |
| `-fcommon` | Required for WinQuake's tentative definitions |

### Key Compiler Flags

| Flag | Purpose |
|------|---------|
| `-mcmodel=medany` | PC-relative addressing (position-independent) |
| `-march=rv32imafc_zicsr_zifencei` | RISC-V with float + CSR + fence |
| `-mabi=ilp32f` | 32-bit ABI with hardware float |
| `-Os` | Size optimization |
| `-DPAPP_APP_SIDE=1` | Excludes loader APIs from psram_app.h |
| `-DIRAM_ATTR=` | Disables IRAM placement attributes |
| `-nostartfiles -nodefaultlibs` | No libc startup (custom entry) |

### Key Linker Flags

| Flag | Purpose |
|------|---------|
| `-T psram_app.ld` | Custom linker script at 0x4A000000 |
| `--entry=app_entry` | Entry point symbol |
| `--wrap=malloc,free,...` | Redirect newlib heap to service table |
| `--gc-sections --no-relax` | Dead code elimination, no linker relaxation |
| `-lc -lgcc -lm` | Newlib, GCC runtime, math library |

### Linker Script (tools/psram_app.ld)

```ld
ENTRY(app_entry)
. = 0x4A000000;           /* PSRAM exec base */

.text    : { KEEP(*(.text.entry)) *(.text*) }
.rodata  : { *(.rodata*) *(.srodata*) }
.data    : { *(.data*) *(.sdata*) }
.bss     : { *(.bss*) *(.sbss*) }
```

All sections in a single contiguous region — enables `medany` code model with PC-relative addressing.

---

## App Shim Layers

PSRAM apps cannot call ESP-IDF functions directly (those are in flash). Shim files bridge the gap between the game engine and the `app_services_t` table.

### OpenTyrian Shims (apps/psram_opentyrian/)

OpenTyrian uses an SDL-like API. Six shim files replace the SDL layer:

| File | Purpose |
|------|---------|
| `papp_main.c` | Entry point (`app_entry`), watchdog task, cleanup |
| `papp_syscalls.c` | Newlib stubs: malloc/free → service table, fd-based file I/O |
| `papp_sdl_video.c` | SDL video shim: 8bpp → RGB565, PPA scale+rotate via `display_write_frame_custom` |
| `papp_sdl_audio.c` | SDL audio shim: mono→stereo, internal DMA RAM, task on core 1 |
| `papp_sdl_event.c` | SDL input: gamepad → SDL key events |
| `papp_sdl_system.c` | SDL system stubs (timers, init/quit) |

### Doom Shims (apps/psram_doom/)

Doom (prboom-go) uses the retro-go framework (`rg_*` APIs) and its own `I_*` interface functions. Six shim files replace the retro-go layer:

| File | Purpose |
|------|---------|
| `papp_main.c` | Entry point, watchdog, calls `Z_Init()` + `D_DoomMain()` via setjmp |
| `papp_syscalls.c` | Generic newlib stubs (shared pattern) + POSIX stubs (`access`, `stat`, `unlink`) |
| `papp_i_video.c` | `I_InitGraphics`, `I_FinishUpdate` (8bpp→RGB565 + PPA 2.4× scale), `I_SetPalette` |
| `papp_i_sound.c` | 8-channel SFX mixer + OPL music synth in dedicated task, `I_InitSound` |
| `papp_i_system.c` | `I_GetTime`, `I_uSleep`, `I_StartTic` (gamepad→Doom key events), `I_SafeExit` |
| `papp_rg_stubs.c` | All retro-go API stubs: `rg_system_*`, `rg_display_*`, `rg_input_*`, `rg_audio_*` |

**Compat headers** in `apps/psram_doom/compat/`:
- `rg_system.h` — Minimal retro-go type/constant definitions (keys, surface, audio types)
- ESP-IDF stubs (copied from OpenTyrian): `esp_attr.h`, `esp_timer.h`, FreeRTOS stubs, etc.

### Quake Shims (apps/psram_quake/)

Quake (WinQuake) uses the retro-go framework like Doom, plus its own `Sys_*`, `VID_*`, `S_*` interfaces. Seven shim files replace the retro-go layer and ESP32-specific subsystems:

| File | Purpose |
|------|----------|
| `papp_main.c` | Entry point, 256KB game task on PSRAM-backed stack, watchdog, cleanup |
| `papp_syscalls.c` | Newlib stubs: 8192-entry alloc tracker, 48-slot FD table, ≥1KB→PSRAM |
| `papp_vid.c` | Video: 320×240 8bpp→RGB565 (native LE), scale 2.0×, double-buffered, 640KB surfcache |
| `papp_snd.c` | Audio: 256 SFX slots, 512-sample buffer, 11025 Hz stereo |
| `papp_input.c` | Gamepad→Quake `Key_Event` (A=fire, B=jump, L/R=strafe, Start=escape) |
| `papp_sys.c` | `Sys_Printf` via log_printf, file I/O, gamma=0.7, 8MB hunk alloc |
| `papp_rg_stubs.c` | ~40 retro-go API stubs |

**Compat headers** in `apps/psram_quake/compat/`: 15 ESP-IDF/FreeRTOS stub headers (same pattern as Doom).

**Key difference from other apps:** Quake requires 256KB task stack (vs 32KB default) because the WinQuake engine uses deep call chains and large local arrays. The `papp_main.c` creates a dedicated game task via `svc->task_create()`, which triggers PSRAM-backed stack allocation in the launcher.

### Malloc Routing (papp_syscalls.c)

- Allocations ≥ 1 KB → **PSRAM** (via `mem_caps_alloc`)
- Allocations < 1 KB → **internal RAM** (via `mem_alloc`)
- All wrapped via linker `--wrap` for `malloc`, `free`, `calloc`, `realloc` + reentrant variants

### File I/O (papp_syscalls.c)

- FD table: 16 entries (fds 3–18), maps to FILE* pointers
- `_open` → `svc->file_open()` → allocate fd
- `_read` / `_write` / `_lseek` / `_close` → service table calls
- **stdout/stderr silenced:** `_write(fd=1|2)` returns count without output (prevents USB JTAG blocking)

### Display

- Both apps: **320×200**, 8-bit indexed color → RGB565 via palette LUT
- `display_write_frame_custom(buf, 320, h, 2.4f, false)` → PPA scale 2.4× + rotate 270° → **480×768** on 480×800 LCD
- OpenTyrian: `SDL_Flip()` → `SDL_SetColors()` builds palette
- Doom: `I_FinishUpdate()` → `I_SetPalette()` uses `V_BuildPalette()` (standard RGB565, no byte-swap)
- Quake: `VID_Update()` → 8bpp→RGB565 via palette LUT (native LE, no byte-swap), scale **2.0×** → 480×640 (smaller than Doom/OT due to 320×240 source)

### Audio

- **OpenTyrian:** 22050 Hz, mono → stereo, SDL callback model, internal DMA RAM, task priority 8, core 1
- **Doom:** 22050 Hz, 8-channel SFX mixer + OPL music synth, directly stereo output, task priority 8, core 1
- **Quake:** 11025 Hz, 256 SFX slots, 512-sample buffer, direct stereo output via `svc->audio_submit()`
- All submit via `svc->audio_submit()` as stereo int16 frames

### Exit / Cleanup

- **Watchdog task** (core 1): polls MENU button every 100ms, triggers exit after 3s hold
- Exit via `longjmp` back to `app_entry()`
- Doom: `D_DoomMain()` never returns — exit via `I_SafeExit()` → `app_return_to_launcher()` → longjmp
- Cleanup sequence:
  1. Delete watchdog task
  2. Stop audio task, free buffers
  3. `papp_cleanup_fds()` — closes all open FILE* handles
  4. Clear display, flush

### Large-Stack Task Creation (PSRAM-backed)

Some engines (e.g. Quake) require large task stacks (256KB+) that can't be allocated from internal SRAM. The `svc->task_create()` service call handles this transparently:

- **Stack ≤ 32KB:** Standard `xTaskCreatePinnedToCore` (internal SRAM stack)
- **Stack > 32KB:** `xTaskCreateStaticPinnedToCore` with:
  - Stack buffer: `heap_caps_malloc(stack_size, MALLOC_CAP_SPIRAM)` (from PSRAM)
  - TCB: `heap_caps_malloc(sizeof(StaticTask_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)` (from internal RAM — FreeRTOS requires TCB in internal memory)

This is implemented in `components/psram_app_loader/psram_app_loader.c` and is transparent to PSRAM apps — they just call `svc->task_create(func, name, stack_size, param, priority, handle)`.

---

## Serial Upload Tool

Upload `.papp` files to the SD card over USB Serial JTAG without removing the card.

### Protocol

```
PC → ESP:  "PAPU"                     (4-byte magic)
PC → ESP:  "/sd/roms/papp/game.papp\n" (destination path)
PC → ESP:  "314244\n"                  (file size, decimal)
ESP → PC:  "\x06READY\n"              (ready, \x06 prefix for parsing)
PC → ESP:  [raw binary data]          (4 KB chunks, ACK per chunk)
ESP → PC:  "\x06OK 314244\n"          (success)
```

### Usage

```bash
python tools/upload_papp.py firmware/opentyrian.papp --port COM30
# Typical: ~140 KB/s, 314 KB in ~2.2 seconds
```

### API (components/serial_upload/)

```c
void serial_upload_init(void);    // Install USB JTAG driver + start listener task
void serial_upload_deinit(void);  // Delete task + uninstall driver (safe teardown)
```

**Critical:** `serial_upload_deinit()` must be called before loading PSRAM apps. The interrupt-driven USB JTAG driver causes multi-second stalls when no USB host is reading.

---

## Hardware Platform

| Component | Spec |
|-----------|------|
| MCU | ESP32-P4, RISC-V dual-core @ 360 MHz |
| PSRAM | 32 MB @ 200 MHz, AP HexaPSRAM |
| Flash | 16 MB, DIO 80 MHz |
| Display | 480×800 MIPI DSI (portrait), ST7701 |
| Audio | ES8311 codec, I2S, 44100 Hz |
| Input | USB HID gamepad, touch panel (GT911) |
| Console | UART0 (GPIO 37/38) @ 115200, USB Serial JTAG on COM30 |
| SD Card | SDHC, SPI @ 20 MHz |
| ESP-IDF | v5.5.2 |

---

## Resolved Issues

| Issue | Root Cause | Fix |
|-------|-----------|-----|
| Choppy audio | DMA buffers in PSRAM, low task priority | Stereo buffer in internal DMA RAM, priority 8 |
| 3rd-run crash | Memory leaks: stereo_buf, open FILE handles | `SDL_CloseAudio()` frees buf, `papp_cleanup_fds()` closes FDs |
| Slow loading (random) | USB Serial JTAG interrupt driver stalling | `serial_upload_deinit()` before load, `serial_upload_init()` after |
| Boot loop crash | `usb_serial_jtag_driver_uninstall()` while task active | Added `serial_upload_deinit()` that deletes task first |
| Launcher reset on X | MENU button → `esp_restart()` in carousel | Disabled `esp_restart()` on MENU press |
| Display too small | Scale 2.0× → 400×640 | Changed to 2.4× → 480×768 |
| Doom wrong colors | Byte-swap in `I_SetPalette` for BE format | Removed byte-swap — `V_BuildPalette` returns standard RGB565 |
| Quake black screen | Scale 2.5× → 600px output ≥ 480px LCD width | Reduced scale to 2.0× → 480×640 fits LCD |
| Quake wrong colors | Palette byte-swap for retro-go BE format | Removed byte-swap — PPA expects native LE RGB565 |
| Quake dark display | Default gamma=1.0 too dark on small LCD | Set gamma=0.7 (lower=brighter in Quake) |
| Quake stack overflow | `CL_ParseServerInfo` 32KB locals on 32KB stack | Made arrays static + 256KB PSRAM-backed task stack |
| Quake pack alloc fail | `heap_caps_malloc(INTERNAL)` for 21KB | Redirect ≥1KB allocs to PSRAM regardless of caps |

---

## Porting a New App

To port a new game/emulator as a PSRAM app:

1. **Create shim files** in `apps/psram_<name>/`:
   - `papp_main.c` — entry point, stores `_papp_svc`, runs main loop
   - `papp_syscalls.c` — copy from OpenTyrian (malloc/file wrappers are generic)
   - Platform-specific shims (video, audio, input) that call `_papp_svc->*`

2. **Add to build script** or create a new `tools/build_<name>_papp.ps1`:
   - List source files
   - Use same compiler/linker flags
   - Adjust `--wrap` list for any additional libc functions

3. **Link at 0x4A000000** using `tools/psram_app.ld`

4. **Pack:** `python tools/pack_papp.py <name>.bin firmware/<name>.papp --bss-size N`

5. **Upload:** `python tools/upload_papp.py firmware/<name>.papp --port COM30`

The app appears in the launcher carousel under the "papp" ROM type.

---

## Doom Porting Notes

The Doom port (prboom-go) has several unique aspects compared to OpenTyrian:

### Engine Architecture

- **70 source files** in `C:\ESPIDFprojects\prboom-go\components\prboom\`
- Uses **retro-go framework** (`rg_*` APIs) instead of SDL — all stubbed in `papp_rg_stubs.c`
- Own **zone memory allocator** (`z_zone.c`): redefines `malloc`→`Z_Malloc`, `free`→`Z_Free` macros — but uses stdlib `malloc()` underneath, so `--wrap=malloc` captures all allocations
- `D_DoomMain()` runs the game loop internally and **never returns** — uses longjmp for exit
- Embedded `prboom.wad` via `prboom_wad.h` (the `PRBOOMWAD` define)

### Key Defines

| Define | Purpose |
|--------|--------|
| `-DRETRO_GO=1` | Enables `#include <rg_system.h>` paths and retro-go API calls |
| `-DHAVE_CONFIG_H=1` | Uses prboom's `config.h` (NODEHSUPPORT, NOTRUECOLOR, PRBOOMWAD) |
| `-DPAPP_APP_SIDE=1` | Standard PSRAM app define |

### Additional POSIX Stubs (vs OpenTyrian)

| Function | Used By | Behavior |
|----------|---------|----------|
| `access()` | `D_AddFile`, `G_RecordDemo` | Opens file to check existence |
| `stat()` / `_stat()` | Save path validation | Returns file size via seek |
| `_unlink()` | Newlib | Returns -1 (not supported) |
| `rename()` | Save system | Returns -1 (not supported) |

### Quake Porting Notes

The Quake port (WinQuake from quake-go) has its own unique aspects:

### Engine Architecture

- **64 source files** in `components/quake/winquake/`
- Uses **retro-go framework** (`rg_*` APIs) — all stubbed in `papp_rg_stubs.c`
- **Hunk allocator**: allocates 8MB contiguous block from PSRAM at startup for all game data
- **Software renderer**: 320×240, 8-bit indexed color, 640KB surface cache
- Game runs in a dedicated task with **256KB PSRAM-backed stack** (WinQuake has deep call chains + large local arrays like `CL_ParseServerInfo`'s 32KB precache arrays, made `static`)

### Key Defines

| Define | Purpose |
|--------|--------|
| `-DESP32_QUAKE=1` | Enables ESP32-specific Quake paths |
| `-DESP_PLATFORM=1` | Provides `MALLOC_CAP_*` defines for heap_caps |
| `-DCONFIG_IDF_TARGET_ESP32P4=1` | ESP32-P4 target identification |
| `-DPAPP_APP_SIDE=1` | Standard PSRAM app define |
| `-fcommon` | Required — WinQuake uses C tentative definitions extensively |

### Display Specifics

- Scale factor: **2.0×** (not 2.4× like Doom/OT — 320×240×2.5 would exceed 480px LCD width)
- Palette: Native little-endian RGB565 (no byte-swap — unlike quake-go which swaps for retro-go's BE format)
- Gamma: Forced to 0.7 (lower = brighter in Quake, compensates for small LCD)
- Double-buffered 320×240 surfaces + zbuffer allocated from PSRAM

### Build Size Comparison

| App | Source Files | Binary Size | BSS Size | Total RAM |
|-----|-------------|------------|----------|----------|
| OpenTyrian | 58 (52 game + 6 shim) | 314 KB | 629 KB | ~943 KB |
| Doom | 76 (70 engine + 6 shim) | 529 KB | 64 KB | ~593 KB |
| Quake | 71 (64 engine + 7 shim) | ~350 KB | ~544 KB | ~894 KB |

---

## Multi-Run Stability & Audio Lifecycle

### Problem: I2S Mutex Deadlock on Second App Launch

When the first PSRAM app (e.g. OpenTyrian) exits and a second app (e.g. Doom) is launched,
the second app would hang forever during `audio_init()`.

**Root cause chain:**

1. App audio task is blocked inside `i2s_channel_write(portMAX_DELAY)`, holding the ESP-IDF I2S driver's internal mutex
2. On exit, `vTaskDelete()` kills the audio task instantly — the mutex is **never released**
3. Launcher calls `audio_reset_sample_rate()` and loads the next app
4. Next app calls `audio_set_sample_rate()` → `i2s_channel_disable()` → tries to acquire the same mutex → **permanent deadlock**

### Fix (3-part)

**1. Graceful audio task exit** — Sound tasks in both apps check `papp_exit_requested` in their main loop and break out cleanly when set. After the loop, they set a `done` flag and spin in a `while(1) delay_ms(100)` (FreeRTOS tasks must never return — returning calls `prvTaskExitError()` → `abort()` → reboot). The cleanup path waits up to 1 second for the `done` flag, then deletes the task.

**2. `audio_reset_sample_rate()`** — New API in `components/audio/audio.c` that sets the cached sample rate to 0. Called by the launcher after every PSRAM app returns, signaling that the I2S state is unknown.

**3. Skip `i2s_channel_disable` when rate=0** — `audio_set_sample_rate()` detects the reset state (cached rate == 0) and only updates the cache without touching the I2S hardware, avoiding any potential mutex interaction.

### Files Modified

| File | Change |
|------|--------|
| `components/audio/audio.c` | Added `audio_reset_sample_rate()`, skip `i2s_channel_disable` when rate=0 |
| `components/audio/include/audio.h` | Declared `audio_reset_sample_rate()` |
| `launcher/main/main.c` | Call `audio_reset_sample_rate()` after papp run returns |
| `apps/psram_opentyrian/papp_sdl_audio.c` | Graceful audio task exit via `papp_exit_requested` + `audio_task_done` |
| `apps/psram_doom/papp_i_sound.c` | Graceful sound task exit via `papp_exit_requested` + `doom_sound_task_done` |

### Known Issue: Memory Leak (~155-220 KB per run)

Each PSRAM app run leaks memory that isn't fully recovered after unload:

| App | Leak per Run |
|-----|-------------|
| OpenTyrian | ~156 KB |
| Doom | ~220 KB |

With 32 MB PSRAM this allows 100+ consecutive runs before problems. To be investigated.

---

## Bug Fix: `_fstat` Missing File Size (Affected Both Apps)

### Problem

`_fstat()` in both `papp_syscalls.c` files only set `st_mode` but left `st_size` **uninitialized** (stack garbage). This caused two issues:

1. **Doom save game crash** — `M_ReadFile()` calls `fseek(fp, 0, SEEK_END)` + `ftell(fp)` to get the file size. Newlib's `fseek(SEEK_END)` uses `_fstat`'s `st_size` internally, so with garbage `st_size` (~1.3 GB), `ftell` returned garbage → `Z_Malloc` tried to allocate 1.3 GB → panic.
2. **OpenTyrian slow loading** — Same mechanism: every `fseek(SEEK_END)` during data file loading used a garbage file size, causing the SD card VFS to attempt huge seeks per file open. Fixing `_fstat` dramatically reduced OpenTyrian's load time.

### Fix

`memset(st, 0, sizeof(*st))` + compute actual file size by seeking the underlying FILE* to end and back:

```c
__builtin_memset(st, 0, sizeof(*st));
void *fp = fd_lookup(fd);
if (fd >= FD_BASE && fp) {
    st->st_mode = S_IFREG;
    long cur = _papp_svc->file_tell(fp);
    _papp_svc->file_seek(fp, 0, SEEK_END);
    st->st_size = _papp_svc->file_tell(fp);
    _papp_svc->file_seek(fp, cur, SEEK_SET);
}
```

### Files Modified

| File | Change |
|------|--------|
| `apps/psram_doom/papp_syscalls.c` | `_fstat` returns correct `st_size` |
| `apps/psram_opentyrian/papp_syscalls.c` | Same fix |

### Status

- OpenTyrian load time: **fixed** (dramatically faster)
- Doom save game load: **still crashes** — `M_ReadFile` reports `length=1341265604` despite the fix. The save file itself may be corrupt from a prior save with the broken `_fstat`, or newlib's `fseek(SEEK_END)` is not using `_fstat` and needs a different fix path (e.g. `_lseek` returning wrong value). To be investigated.
