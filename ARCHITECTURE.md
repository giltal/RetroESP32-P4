# RetroESP32-P4 — Architecture & Project Documentation

## Table of Contents

1. [Overview](#overview)
2. [Hardware Platform](#hardware-platform)
3. [OTA Multi-Binary Architecture](#ota-multi-binary-architecture)
4. [Flash Partition Layout](#flash-partition-layout)
5. [Project Directory Structure](#project-directory-structure)
6. [Shared Components](#shared-components)
7. [Launcher (Factory App)](#launcher-factory-app)
8. [Emulator Apps](#emulator-apps)
9. [Inter-App Communication (NVS)](#inter-app-communication-nvs)
10. [App Lifecycle Flow](#app-lifecycle-flow)
11. [Safe Boot](#safe-boot)
12. [SDK Configuration](#sdk-configuration)
13. [Build System](#build-system)
14. [Flash Procedure](#flash-procedure)
15. [Adding a New Emulator](#adding-a-new-emulator)
16. [Binary Size Reference](#binary-size-reference)
17. [Troubleshooting](#troubleshooting)

---

## Overview

RetroESP32-P4 is a multi-system retro game emulator running on the **ESP32-P4** microcontroller. It supports the following systems:

| System                      | Emulator Core | OTA Slot | File Extensions     |
|-----------------------------|---------------|----------|---------------------|
| Nintendo Entertainment System (NES) | nofrendo  | ota_0    | `.nes`              |
| Game Boy / Game Boy Color   | gnuboy        | ota_1    | `.gb`, `.gbc`       |
| Sega Master System / Game Gear / ColecoVision | smsplus | ota_2 | `.sms`, `.gg`, `.col` |
| ZX Spectrum                 | spectrum      | ota_3    | `.z80`, `.sna`      |
| Atari 2600                  | Stella        | ota_4    | `.a26`, `.bin`      |
| Atari 7800                  | ProSystem     | ota_5    | `.a78`              |
| Atari Lynx                  | Handy         | ota_6    | `.lnx`              |
| *(Reserved)* PC Engine      | —             | ota_7    | `.pce`              |
| *(Reserved)* Atari 800      | —             | ota_8    | `.xex`, `.atr`      |

The project follows the **OTA multi-binary architecture** from the original RetroESP32: each emulator is compiled as a standalone firmware binary and occupies its own flash partition. The **launcher** (menu/UI) lives in the factory partition and switches to the appropriate emulator via ESP-IDF's OTA partition API.

---

## Hardware Platform

| Component       | Specification                                    |
|-----------------|--------------------------------------------------|
| **MCU**         | ESP32-P4, RISC-V dual-core @ 360 MHz            |
| **Flash**       | 16 MB, DIO mode, 80 MHz                         |
| **PSRAM**       | 32 MB, HEX (octal) mode, 200 MHz                |
| **Internal SRAM** | 768 KB                                        |
| **Display**     | 4.3" 480×800 MIPI DSI LCD (ST7701S driver)      |
| **Touch**       | GT911 capacitive touch controller (I2C)          |
| **Audio**       | ES8311 codec via I2S, power amplifier on GPIO 11 |
| **Input**       | USB HID gamepad (via USB Host)                   |
| **SD Card**     | SDMMC interface, FAT32 with LFN support          |
| **Framework**   | ESP-IDF v5.5.2                                   |

---

## OTA Multi-Binary Architecture

### Why OTA Multi-Binary?

A monolithic single-binary containing all emulators exceeded the ESP32-P4's IRAM and DRAM limits (~2.6 MB of 3 MB used, DRAM 92.25%). The OTA approach solves this by:

- **Isolating each emulator** into its own firmware binary with independent memory layout.
- **Eliminating link-time conflicts** between emulator cores (duplicate symbols, incompatible globals).
- **Allowing larger emulators** (e.g., Stella at 1.1 MB, smsplus at 1.2 MB) that would not fit alongside others.
- **Reserving slots for future emulators** without recompiling existing ones.

### How It Works

```
┌──────────────────────────────────────────────────────┐
│                     BOOT FLOW                         │
│                                                       │
│  Power On ──► Bootloader ──► otadata check            │
│                                 │                     │
│              ┌──────────────────┼──────────────┐      │
│              ▼                  ▼               ▼     │
│         factory            ota_0..8         (default) │
│        (launcher)         (emulators)       = factory │
│              │                  │                     │
│              │   User selects   │                     │
│              │   a ROM file     │                     │
│              ├──────────────────┤                     │
│              │ 1. Save ROM path │                     │
│              │    to NVS        │                     │
│              │ 2. Set OTA boot  │                     │
│              │    partition     │                     │
│              │ 3. esp_restart() │                     │
│              ▼                  │                     │
│         Emulator boots         │                     │
│         from ota_N             │                     │
│              │                  │                     │
│              │ Emulator exits   │                     │
│              │ or safe-boot     │                     │
│              ├──────────────────┤                     │
│              │ 1. Set OTA boot  │                     │
│              │    = factory     │                     │
│              │ 2. esp_restart() │                     │
│              ▼                  │                     │
│         Launcher boots again   │                     │
└──────────────────────────────────────────────────────┘
```

The ESP-IDF `otadata` partition tracks which app partition to boot next. The launcher sets the boot partition via `esp_ota_set_boot_partition()` and reboots; the emulator does the same to return to the launcher.

---

## Flash Partition Layout

The 16 MB flash is divided as follows (defined in `partitions_ota.csv`):

| Partition  | Type | SubType | Offset     | Size    | Contents             |
|------------|------|---------|------------|---------|----------------------|
| nvs        | data | nvs     | 0x009000   | 16 KB   | NVS key-value store  |
| otadata    | data | ota     | 0x00D000   | 8 KB    | OTA boot selection   |
| factory    | app  | factory | 0x010000   | 1 MB    | Launcher (menu/UI)   |
| ota_0      | app  | ota_0   | 0x110000   | 896 KB  | NES (nofrendo)       |
| ota_1      | app  | ota_1   | 0x1F0000   | 896 KB  | GB/GBC (gnuboy)      |
| ota_2      | app  | ota_2   | 0x2D0000   | 1.5 MB  | SMS/GG/COL (smsplus) |
| ota_3      | app  | ota_3   | 0x450000   | 896 KB  | ZX Spectrum          |
| ota_4      | app  | ota_4   | 0x530000   | 1.5 MB  | Atari 2600 (Stella)  |
| ota_5      | app  | ota_5   | 0x6B0000   | 896 KB  | Atari 7800 (ProSystem) |
| ota_6      | app  | ota_6   | 0x790000   | 896 KB  | Atari Lynx (Handy)   |
| ota_7      | app  | ota_7   | 0x870000   | 1.5 MB  | *(Reserved: PCE)*    |
| ota_8      | app  | ota_8   | 0x9F0000   | 1.5 MB  | *(Reserved: A800)*   |

**Note:** The bootloader lives at offset `0x2000` (ESP32-P4 requirement) and the partition table at `0x8000`.

---

## Project Directory Structure

```
RetroESP32_P4/
├── partitions_ota.csv           # OTA partition table (shared by all projects)
├── build_all.ps1                # Build script: launcher + all emulators
├── flash_all.ps1                # Flash script: all binaries via esptool
│
├── launcher/                    # ── LAUNCHER (factory partition) ──
│   ├── CMakeLists.txt           #   ESP-IDF project file
│   ├── sdkconfig.defaults       #   SDK configuration
│   └── main/
│       ├── CMakeLists.txt       #   Component registration
│       ├── main.c               #   Menu UI, ROM browser, OTA switching
│       ├── pins_config.h        #   GPIO pin definitions
│       ├── includes/            #   UI headers (core.h, carousel, etc.)
│       └── sprites/             #   PNG sprites for the UI
│
├── apps/                        # ── EMULATOR APPS (one per OTA slot) ──
│   ├── sdkconfig_common.defaults#   Shared sdkconfig for all emulator apps
│   ├── nes/                     #   NES app → ota_0
│   │   ├── CMakeLists.txt
│   │   ├── sdkconfig.defaults   #   Copy of sdkconfig_common.defaults
│   │   └── main/
│   │       ├── CMakeLists.txt   #   REQUIRES: app_common nofrendo
│   │       └── main.c           #   app_init → get_rom → nofrendo_run → return
│   ├── gb/                      #   GB/GBC app → ota_1
│   ├── sms/                     #   SMS/GG/COL app → ota_2
│   ├── spectrum/                #   ZX Spectrum app → ota_3
│   ├── stella/                  #   Atari 2600 app → ota_4
│   ├── prosystem/               #   Atari 7800 app → ota_5
│   └── handy/                   #   Atari Lynx app → ota_6
│
├── components/                  # ── SHARED COMPONENTS (used by all projects) ──
│   ├── app_common/              #   Shared init/exit for emulator apps
│   ├── odroid/                  #   HAL: system, audio, display, input, SD, settings
│   ├── st7701_lcd/              #   MIPI DSI LCD driver (ST7701S)
│   ├── gt911_touch/             #   Touch controller driver
│   ├── ppa_engine/              #   PPA 2D graphics acceleration
│   ├── gamepad/                 #   USB HID gamepad abstraction
│   ├── audio/                   #   ES8311 I2S audio driver
│   ├── usb_host_hid/            #   USB Host HID stack
│   ├── pngaux/                  #   PNG sprite loading utilities
│   ├── pngdec/                  #   PNG decoder
│   ├── nofrendo/                #   NES emulator core
│   ├── gnuboy/                  #   Game Boy emulator core
│   ├── smsplus/                 #   SMS/GG/COL emulator core
│   ├── spectrum/                #   ZX Spectrum emulator core
│   ├── stella/                  #   Atari 2600 emulator core
│   ├── prosystem/               #   Atari 7800 emulator core
│   └── handy/                   #   Atari Lynx emulator core
│
├── firmware/                    # Collected binaries (output of build_all.ps1)
├── main/                        # Legacy monolithic main (unused in OTA build)
└── managed_components/          # ESP-IDF component manager dependencies
```

---

## Shared Components

### `app_common` — Emulator App Framework

The `app_common` component provides the standard lifecycle for all emulator apps:

```c
/* app_common.h — Public API */

void app_init(void);
//   Full hardware initialization:
//   NVS → odroid_system_init() → audio → gamepad → SD mount → safe boot check

void app_check_safe_boot(void);
//   Polls gamepad for ~500ms; if button A is held, returns to launcher.
//   Called automatically at the end of app_init().

int  app_get_rom_path(char *buf, int buflen);
//   Reads the ROM file path from NVS (written by the launcher).
//   Returns 0 on success, -1 if no path was set.

void app_return_to_launcher(void) __attribute__((noreturn));
//   Sets OTA boot partition to factory, then calls esp_restart().
//   Does not return.
```

**Dependencies:** `odroid`, `nvs_flash`, `esp_partition`, `app_update`, `esp_system`

### `odroid` — Hardware Abstraction Layer

The `odroid` component (ported from the original RetroESP32) provides:

| Module              | Purpose                                      |
|---------------------|----------------------------------------------|
| `odroid_system`     | I2C, PPA, USB host, LCD, touch init          |
| `odroid_audio`      | I2S + ES8311 codec (init, write, volume)     |
| `odroid_display`    | MIPI DSI frame buffer, PPA scaling/rotation  |
| `odroid_input`      | USB HID gamepad polling                      |
| `odroid_sdcard`     | SDMMC mount/unmount (FAT32)                  |
| `odroid_settings`   | NVS read/write (ROM path, volume, brightness)|

### Other Hardware Components

| Component       | Purpose                                          |
|-----------------|--------------------------------------------------|
| `st7701_lcd`    | ST7701S MIPI DSI panel initialization & commands |
| `gt911_touch`   | GT911 touch controller I2C driver                |
| `ppa_engine`    | ESP32-P4 PPA (Pixel Processing Accelerator)      |
| `gamepad`       | High-level gamepad event abstraction             |
| `audio`         | Low-level I2S + codec initialization             |
| `usb_host_hid`  | USB Host HID device enumeration & reports        |
| `pngaux/pngdec` | PNG decoding for UI sprites                      |

---

## Launcher (Factory App)

The launcher is a full-screen graphical menu that boots from the **factory** partition. It provides:

- **Carousel view** — System selection with animated PNG icons.
- **ROM browser** — SD card file browser with long filename support.
- **ROM execution** — OTA partition switching to launch emulators.

### Key Functions

#### `get_ota_slot(char *ext)` — Extension-to-Slot Mapping

Maps a ROM file extension to the corresponding OTA partition slot number:

```c
int get_ota_slot(char* ext) {
    if(ext_eq(ext, "nes")) return 0;   // ota_0: nofrendo
    if(ext_eq(ext, "gb"))  return 1;   // ota_1: gnuboy
    if(ext_eq(ext, "gbc")) return 1;   // ota_1: gnuboy
    if(ext_eq(ext, "sms")) return 2;   // ota_2: smsplus
    if(ext_eq(ext, "gg"))  return 2;   // ota_2: smsplus
    if(ext_eq(ext, "col")) return 2;   // ota_2: smsplus
    if(ext_eq(ext, "z80")) return 3;   // ota_3: spectrum
    if(ext_eq(ext, "sna")) return 3;   // ota_3: spectrum
    if(ext_eq(ext, "a26")) return 4;   // ota_4: stella
    if(ext_eq(ext, "bin")) return 4;   // ota_4: stella
    if(ext_eq(ext, "a78")) return 5;   // ota_5: prosystem
    if(ext_eq(ext, "lnx")) return 6;   // ota_6: handy
    if(ext_eq(ext, "pce")) return 7;   // ota_7: (reserved)
    if(ext_eq(ext, "xex")) return 8;   // ota_8: (reserved)
    if(ext_eq(ext, "atr")) return 8;   // ota_8: (reserved)
    return -1;
}
```

#### `rom_run(bool resume)` — Launch an Emulator

```
1. Save ROM path to NVS:         odroid_settings_RomFilePath_set(rom_path)
2. Save resume state to NVS:     odroid_settings_DataSlot_set(resume ? 1 : 0)
3. Look up OTA slot:             get_ota_slot(ROM.ext)
4. Find the partition:           esp_partition_find_first(APP, OTA_MIN + slot)
5. Set boot partition:           esp_ota_set_boot_partition(emu_part)
6. Reboot:                       esp_restart()
```

### Launcher Component Dependencies

The launcher links **only** hardware/UI components — no emulator cores:

```cmake
REQUIRES odroid st7701_lcd gt911_touch ppa_engine gamepad audio pngaux
         driver esp_lcd esp_psram fatfs vfs sdmmc nvs_flash
         esp_partition esp_app_format esp_event app_update
```

---

## Emulator Apps

Each emulator app is a minimal ESP-IDF project with a standardized `app_main()`:

```c
#include <stdio.h>
#include "app_common.h"
#include "<emulator>_run.h"

void app_main(void)
{
    app_init();                          // Hardware init + safe boot check

    char rom_path[256];
    if (app_get_rom_path(rom_path, sizeof(rom_path)) != 0) {
        printf("No ROM path, returning to launcher\n");
        app_return_to_launcher();        // No ROM → go back
    }

    <emulator>_run(rom_path);            // Run the emulator (blocks)

    app_return_to_launcher();            // Emulator exited → go back
}
```

### CMake Structure

Each emulator app has a **project-level** and **main-component** CMakeLists.txt:

**Project (`apps/<emu>/CMakeLists.txt`):**
```cmake
cmake_minimum_required(VERSION 3.16)
set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/../../components")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(<emu>_app)
```

**Main component (`apps/<emu>/main/CMakeLists.txt`):**
```cmake
idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES app_common <emulator_component>
)
```

Each emulator app only links `app_common` + its own emulator core component. This keeps binaries small and avoids cross-emulator symbol conflicts.

---

## Inter-App Communication (NVS)

The launcher and emulator apps communicate through the **NVS (Non-Volatile Storage)** partition using the namespace `"Odroid"`:

| NVS Key        | Set By     | Read By   | Purpose                              |
|----------------|------------|-----------|--------------------------------------|
| `RomFilePath`  | Launcher   | Emulator  | Full SD card path to the ROM file    |
| `DataSlot`     | Launcher   | Emulator  | Resume slot (0 = fresh, 1 = resume)  |
| `StartAction`  | Launcher   | Emulator  | Start mode (new game vs. restart)    |
| `Volume`       | Either     | Either    | Audio volume level                   |
| `Brightness`   | Either     | Either    | Display brightness                   |

**API (from `odroid_settings.h`):**
```c
char* odroid_settings_RomFilePath_get(void);
void  odroid_settings_RomFilePath_set(const char* path);
int   odroid_settings_DataSlot_get(void);
void  odroid_settings_DataSlot_set(int slot);
int   odroid_settings_StartAction_get(void);
void  odroid_settings_StartAction_set(int action);
```

---

## App Lifecycle Flow

### Full Round-Trip Sequence

```
 ┌─────────────────────────────────────────────────────────┐
 │ 1. POWER ON                                             │
 │    Bootloader → otadata says "factory" → Launcher boots │
 ├─────────────────────────────────────────────────────────┤
 │ 2. LAUNCHER RUNNING                                     │
 │    User browses carousel → picks a system → browses ROMs│
 │    User selects "Super Mario Bros.nes"                  │
 ├─────────────────────────────────────────────────────────┤
 │ 3. LAUNCHER: rom_run()                                  │
 │    NVS write: RomFilePath = "/sd/roms/nes/Super Mario   │
 │               Bros.nes"                                 │
 │    NVS write: DataSlot = 0                              │
 │    OTA set boot: ota_0 (0x110000)                       │
 │    esp_restart()                                        │
 ├─────────────────────────────────────────────────────────┤
 │ 4. REBOOT → NES APP (ota_0)                             │
 │    app_init():                                          │
 │      NVS init                                           │
 │      Hardware init (I2C, PPA, USB, audio, LCD, touch)   │
 │      Mount SD card                                      │
 │      Safe boot check (~500ms, button A → back to #1)    │
 │    app_get_rom_path() → "/sd/roms/nes/Super Mario..."   │
 │    nofrendo_run(rom_path) → [plays game]                │
 ├─────────────────────────────────────────────────────────┤
 │ 5. USER EXITS GAME (or emulator crashes gracefully)     │
 │    app_return_to_launcher():                            │
 │      OTA set boot: factory (0x10000)                    │
 │      esp_restart()                                      │
 ├─────────────────────────────────────────────────────────┤
 │ 6. REBOOT → LAUNCHER (factory)                          │
 │    Back to carousel — ready for another ROM              │
 └─────────────────────────────────────────────────────────┘
```

---

## Safe Boot

If an emulator crashes or hangs during startup, the device would be stuck on that emulator. The **safe boot** feature prevents this:

- During `app_init()`, the gamepad is polled **10 times over ~500 ms**.
- If **button A** is held during this window, the app immediately calls `app_return_to_launcher()`.
- This returns to the factory partition (launcher) without running the emulator.

**User instruction:** If the device boots into a broken emulator, hold the **A button** during the first second of boot to return to the menu.

---

## SDK Configuration

All projects share a common `sdkconfig.defaults` base. The launcher has its own in `launcher/sdkconfig.defaults`, and all emulator apps share `apps/sdkconfig_common.defaults`.

### Key Settings

```ini
# Target
CONFIG_IDF_TARGET="esp32p4"

# PSRAM: 32MB hex mode at 200MHz
CONFIG_SPIRAM=y
CONFIG_SPIRAM_MODE_HEX=y
CONFIG_SPIRAM_SPEED_200M=y
CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY=y

# Flash: 16MB
CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y

# Partition table: shared OTA layout
CONFIG_PARTITION_TABLE_CUSTOM=y
CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="<relative path>/partitions_ota.csv"

# FAT long filenames (CRITICAL for ROM browsing)
CONFIG_FATFS_LFN_HEAP=y
CONFIG_FATFS_MAX_LFN=255

# Size optimizations
CONFIG_COMPILER_OPTIMIZATION_SIZE=y
CONFIG_BOOTLOADER_COMPILER_OPTIMIZATION_SIZE=y
CONFIG_COMPILER_OPTIMIZATION_ASSERTIONS_DISABLE=y
CONFIG_RINGBUF_PLACE_FUNCTIONS_INTO_FLASH=y
CONFIG_FREERTOS_PLACE_FUNCTIONS_INTO_FLASH=y
```

> **Important:** The `CONFIG_FATFS_LFN_HEAP=y` setting is essential. Without it, filenames are truncated to 8.3 format, breaking ROM browsing.

---

## Build System

### Prerequisites

- **ESP-IDF v5.5.2** installed at `C:\Users\<user>\esp\v5.5.2\esp-idf`
- **Python 3.11** environment at `C:\Users\<user>\.espressif\python_env\idf5.5_py3.11_env`
- **CMake 3.16+** and **Ninja** (bundled with ESP-IDF)

### Build All (`build_all.ps1`)

Builds the launcher and all 7 emulator apps sequentially, collecting binaries into `firmware/`:

```powershell
.\build_all.ps1
```

**What it does:**
1. Loads ESP-IDF environment (`export.ps1`)
2. Builds `launcher/` → copies `launcher.bin`, `bootloader.bin`, `partition-table.bin`
3. Builds each `apps/<emu>/` → copies `<emu>_app.bin`
4. All binaries collected in `firmware/`

### Build a Single App

To rebuild only one emulator:

```powershell
# Load ESP-IDF environment first
$env:IDF_PYTHON_ENV_PATH = "C:\Users\<user>\.espressif\python_env\idf5.5_py3.11_env"
& "C:\Users\<user>\esp\v5.5.2\esp-idf\export.ps1"

# Build just the NES app
cd apps\nes
idf.py build
```

### Clean Rebuild

```powershell
cd launcher; idf.py fullclean; cd ..
cd apps\nes; idf.py fullclean; cd ..\..
# ... repeat for each app
.\build_all.ps1
```

---

## Flash Procedure

### Flash All (`flash_all.ps1`)

Flashes all binaries to the ESP32-P4 via serial (COM30):

```powershell
.\flash_all.ps1
```

**Flash map (11 binaries):**

| Offset     | Binary                | Source                        |
|------------|-----------------------|-------------------------------|
| 0x2000     | bootloader.bin        | launcher/build/bootloader/    |
| 0x8000     | partition-table.bin   | launcher/build/partition_table/|
| 0xD000     | ota_data_initial.bin  | launcher/build/               |
| 0x10000    | launcher.bin          | launcher/build/               |
| 0x110000   | nes_app.bin           | apps/nes/build/               |
| 0x1F0000   | gb_app.bin            | apps/gb/build/                |
| 0x2D0000   | sms_app.bin           | apps/sms/build/               |
| 0x450000   | spectrum_app.bin      | apps/spectrum/build/          |
| 0x530000   | stella_app.bin        | apps/stella/build/            |
| 0x6B0000   | prosystem_app.bin     | apps/prosystem/build/         |
| 0x790000   | handy_app.bin         | apps/handy/build/             |

**Important:**
- The bootloader offset is **0x2000** (ESP32-P4 specific, not 0x0 like ESP32).
- The `ota_data_initial.bin` at **0xD000** initializes the OTA data to boot the factory partition by default.

### Flash a Single App

To update only one emulator without reflashing everything:

```powershell
python -m esptool --chip esp32p4 -p COM30 -b 460800 `
    --before default_reset --after hard_reset write_flash `
    --flash_mode dio --flash_size 16MB --flash_freq 80m `
    0x110000 firmware\nes_app.bin
```

---

## Adding a New Emulator

To add a new emulator (e.g., PC Engine in ota_7):

### 1. Create the Emulator Core Component

```
components/
└── pce/
    ├── CMakeLists.txt
    ├── include/
    │   └── pce_run.h          # void pce_run(const char *rom_path);
    └── *.c / *.cpp            # Emulator source files
```

### 2. Create the App Project

```
apps/
└── pce/
    ├── CMakeLists.txt
    ├── sdkconfig.defaults     # Copy of apps/sdkconfig_common.defaults
    └── main/
        ├── CMakeLists.txt     # REQUIRES app_common pce
        └── main.c             # Standard pattern (see Emulator Apps section)
```

**`apps/pce/CMakeLists.txt`:**
```cmake
cmake_minimum_required(VERSION 3.16)
set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/../../components")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(pce_app)
```

**`apps/pce/main/main.c`:**
```c
#include <stdio.h>
#include "app_common.h"
#include "pce_run.h"

void app_main(void)
{
    app_init();
    char rom_path[256];
    if (app_get_rom_path(rom_path, sizeof(rom_path)) != 0) {
        app_return_to_launcher();
    }
    pce_run(rom_path);
    app_return_to_launcher();
}
```

### 3. Register Extensions in the Launcher

In `launcher/main/main.c`, the `get_ota_slot()` function already has the mapping:
```c
if(ext_eq(ext, "pce")) return 7;   // ota_7: huexpress
```

If you need new extensions, add them to this function.

### 4. Verify Partition Size

Check that the compiled binary fits in the partition. The ota_7 slot has **1.5 MB** available. If the binary is larger, adjust `partitions_ota.csv` (and update all downstream offsets).

### 5. Update Build & Flash Scripts

Add the new app to `build_all.ps1` and `flash_all.ps1`:

**`build_all.ps1`** — add to the `$apps` array:
```powershell
@{ Name = "pce"; Dir = "apps\pce"; Bin = "pce_app.bin" }
```

**`flash_all.ps1`** — add to the `$flash_map` array:
```powershell
@{ Offset = "0x870000"; File = "pce_app.bin"; Desc = "PCE (ota_7)" }
```

---

## Binary Size Reference

| Binary              | Size    | Partition | Capacity | Headroom |
|---------------------|---------|-----------|----------|----------|
| launcher.bin        | 624 KB  | factory   | 1 MB     | 400 KB   |
| nes_app.bin         | 546 KB  | ota_0     | 896 KB   | 350 KB   |
| gb_app.bin          | 518 KB  | ota_1     | 896 KB   | 378 KB   |
| sms_app.bin         | 1210 KB | ota_2     | 1.5 MB   | 326 KB   |
| spectrum_app.bin    | 578 KB  | ota_3     | 896 KB   | 318 KB   |
| stella_app.bin      | 1161 KB | ota_4     | 1.5 MB   | 375 KB   |
| prosystem_app.bin   | 553 KB  | ota_5     | 896 KB   | 343 KB   |
| handy_app.bin       | 512 KB  | ota_6     | 896 KB   | 384 KB   |

Total flash used by application binaries: **~5.7 MB** of 16 MB.

---

## Troubleshooting

### Device boots into broken emulator (stuck)

**Solution:** Hold **button A** during the first second after reboot. The safe boot check in `app_init()` will detect it and return to the launcher.

### Filenames show as 8.3 truncated names

**Cause:** Missing FATFS long filename configuration.
**Solution:** Ensure both `launcher/sdkconfig.defaults` and `apps/sdkconfig_common.defaults` contain:
```ini
CONFIG_FATFS_LFN_HEAP=y
CONFIG_FATFS_MAX_LFN=255
```
Then do a **full clean rebuild** (`idf.py fullclean` in each project before building).

### Binary too large for partition

**Symptom:** Flash fails or app crashes on boot.
**Solution:** Check binary size against partition capacity in the table above. Either optimize the emulator code (`-Os`, strip debug info) or resize the partition in `partitions_ota.csv` (adjust all subsequent offsets).

### OTA partition not found

**Symptom:** `rom_run: OTA partition ota_N not found!` in launcher logs.
**Cause:** Partition table mismatch — the running firmware was built with a different `partitions_ota.csv`.
**Solution:** Reflash the partition table: `esptool write_flash 0x8000 firmware/partition-table.bin`

### Emulator boots but no ROM loads

**Symptom:** Emulator immediately returns to launcher.
**Cause:** NVS ROM path not set or SD card not mounted.
**Solution:** Check serial monitor for `"No ROM path found in NVS"` or SD card mount errors. Ensure the SD card is inserted and formatted as FAT32.

---

*Document generated for RetroESP32-P4 OTA multi-binary architecture, ESP-IDF v5.5.2*
