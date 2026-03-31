# ESP32-P4 PAPP Template — Context

## What is this?

A **template project** for building PAPP (PSRAM Application) binaries that run on the RetroESP32-P4 handheld. PAPP apps are position-independent RISC-V flat binaries, loaded from SD card into PSRAM and executed via MMU mapping by the launcher firmware.

## Hardware

| Feature | Detail |
|---------|--------|
| **SoC** | ESP32-P4 dual-core RISC-V, 360 MHz |
| **RAM** | 32 MB PSRAM (OPI) |
| **Flash** | 16 MB |
| **LCD** | 480×800 MIPI DSI portrait (ST7701 panel) |
| **Touch** | GT911 capacitive (I2C bus 1, SDA=GPIO7, SCL=GPIO8) |
| **Audio** | ES8311 codec via I2S |
| **Gamepad** | USB HID (PS4/PS5/generic), 14 buttons + D-pad + analog sticks |
| **Paddle** | GPIO 52, ADC2_CH3, 12-bit |
| **SD Card** | SD/MMC, mounted at `/sd/` |

## Display Pipeline

The physical LCD is **480×800 portrait**. PAPP apps draw into a landscape framebuffer, then `display_write_frame_custom()` handles the PPA hardware rotation + scaling to fill the LCD:

```
400×240 framebuffer → PPA rotate 270° CCW → 240×400 → scale 2× → 480×800 → LCD (perfect fit, no borders)
```

- **Framebuffer size:** 400×240 RGB565 (192,000 bytes)
- **Coordinate system:** (0,0) = top-left landscape, X right (0–399), Y down (0–239)
- **Flush call:** `svc->display_write_frame_custom(fb, 400, 240, 2.0f, false)`

Other display options:
- `display_get_framebuffer()` → direct 800×480 native framebuffer (no rotation needed)
- `display_write_frame_rgb565(buf)` → standard 320×240 with default rotate+scale
- `display_write_frame_custom(buf, W, H, scale, byte_swap)` → any resolution + auto rotate 270° + uniform scale

## App Services API

PAPP apps cannot call ESP-IDF or libc directly. All functionality goes through the `app_services_t` function pointer table passed to `app_entry()`:

### Display
| Function | Purpose |
|----------|---------|
| `display_get_framebuffer()` | Get pointer to 800×480 native RGB565 FB |
| `display_get_emu_buffer()` | Get pointer to 320×240 emu buffer |
| `display_flush()` | Flush native FB with rotate+scale |
| `display_clear(color)` | Fill entire screen with solid RGB565 color |
| `display_set_scale(sx, sy)` | Override PPA scale factors |
| `display_write_frame_rgb565(buf)` | Submit 320×240 frame |
| `display_write_frame_custom(buf, w, h, scale, swap)` | Submit arbitrary-size frame with PPA |
| `display_write_rect(x, y, w, h, data)` | Draw rect to native FB |
| `display_lock()` / `display_unlock()` | Display mutex |

### Audio
| Function | Purpose |
|----------|---------|
| `audio_init(sample_rate)` | Init audio (22050, 32000, 44100 Hz) |
| `audio_submit(buf, frames)` | Submit 16-bit signed stereo interleaved PCM |

### Input
| Function | Purpose |
|----------|---------|
| `input_gamepad_read(&state)` | Read gamepad — `state.values[PAPP_INPUT_*]` |

**Buttons:** `PAPP_INPUT_UP`, `DOWN`, `LEFT`, `RIGHT`, `SELECT`, `START`, `A`, `B`, `X`, `Y`, `L`, `R`, `MENU`, `VOLUME`

### File I/O
| Function | Purpose |
|----------|---------|
| `file_open(path, mode)` | Open file (SD card at `/sd/`) |
| `file_close(stream)` | Close file |
| `file_read(ptr, size, n, stream)` | Read from file |
| `file_write(ptr, size, n, stream)` | Write to file |
| `file_seek(stream, offset, whence)` | Seek in file |
| `file_tell(stream)` | Get file position |

### Memory
| Function | Purpose |
|----------|---------|
| `mem_alloc(size)` | malloc (PSRAM) |
| `mem_calloc(n, size)` | calloc (PSRAM) |
| `mem_realloc(ptr, size)` | realloc |
| `mem_free(ptr)` | free |
| `mem_caps_alloc(size, caps)` | Allocate with caps (SPIRAM/INTERNAL/DMA) |

**Caps:** `PAPP_MEM_CAP_SPIRAM` (0x400), `PAPP_MEM_CAP_INTERNAL` (0x800), `PAPP_MEM_CAP_DMA` (0x04)

### System
| Function | Purpose |
|----------|---------|
| `log_printf(fmt, ...)` | Printf to USB JTAG (COM30) |
| `delay_ms(ms)` | Sleep (FreeRTOS vTaskDelay) |
| `get_time_us()` | Microsecond timestamp |

### Settings (NVS)
| Function | Purpose |
|----------|---------|
| `settings_rom_path_get()` | Get current ROM path |
| `settings_volume_get()` / `set()` | Volume 0–4 |
| `settings_brightness_get()` / `set()` | Brightness level |

### Tasks
| Function | Purpose |
|----------|---------|
| `task_create(fn, name, stack, arg, prio, handle, core)` | Create FreeRTOS task |
| `task_delete(handle)` | Delete task |

## Building

### Prerequisites
- ESP-IDF v5.5.2 environment sourced (provides `riscv32-esp-elf` toolchain)
- Launcher firmware flashed and running on the device

### Build Command
```powershell
# Source ESP-IDF
$env:IDF_PYTHON_ENV_PATH = "C:\Users\97254\.espressif\python_env\idf5.5_py3.12_env"
& "C:\Users\97254\esp\v5.5.2\esp-idf\export.ps1" 2>$null

# Build
.\tools\build_psram_app.ps1 -AppName ESP32_P4_PAPP_Template -Sources ESP32_P4_PAPP_Template\main.c
```

For multiple source files:
```powershell
.\tools\build_psram_app.ps1 -AppName MyApp -Sources "src\main.c","src\game.c","src\render.c" -ExtraIncludes "src\include"
```

### Upload to Device
```powershell
python tools\upload_papp.py firmware\ESP32_P4_PAPP_Template.papp --port COM30
```

The file lands at `/sd/roms/papp/ESP32_P4_PAPP_Template.papp` and appears in the launcher's PAPP carousel.

## PAPP App Rules

1. **`#define PAPP_APP_SIDE 1`** before `#include "psram_app.h"`
2. **Entry function** must be `__attribute__((section(".text.entry"))) int app_entry(const app_services_t *svc)`
3. **No direct ESP-IDF calls** — everything goes through `svc->` function pointers
4. **No libc** — use `svc->mem_alloc()` instead of `malloc()`, `svc->log_printf()` instead of `printf()`
5. **Return 0** for clean exit, negative for error
6. **MENU button** — check `pad.values[PAPP_INPUT_MENU]` to let user exit
7. **Clean up** before returning — delete tasks, close files, free memory
8. **Compiled with** `-mcmodel=medany -nostdlib` — position-independent, no standard library

## Linker Script

Located at `tools/psram_app.ld`. Maps everything to `0x4A000000` (PSRAM exec virtual address):
- `.text.entry` first (entry point at offset 0)
- `.text` / `.rodata` / `.data` / `.bss`

## File Structure

```
ESP32_P4_PAPP_Template/
├── main.c          ← Your app code (modify this)
└── CONTEXT.md      ← This file
```

Build outputs:
```
build_papp/ESP32_P4_PAPP_Template/    ← Intermediates (.o, .elf, .asm, .bin)
firmware/ESP32_P4_PAPP_Template.papp  ← Final binary for SD card
```

## Tips

- **Custom resolution:** Change `FB_W`/`FB_H` and adjust the scale in `display_write_frame_custom()`.  
  Formula: `out_w = FB_H × scale`, `out_h = FB_W × scale`. Must fit within 480×800.
- **Audio:** Init once with `svc->audio_init(22050)`, then submit frames in your main loop or a dedicated task.
- **File access:** All paths are absolute from SD root, e.g. `/sd/roms/papp/data/level1.dat`.
- **Dynamic memory:** Use `svc->mem_alloc()` / `svc->mem_free()`. For DMA buffers, use `svc->mem_caps_alloc(size, PAPP_MEM_CAP_DMA | PAPP_MEM_CAP_SPIRAM)`.
- **Complex apps:** See `apps/psram_doom/` for examples of linking newlib (`-lc -lgcc -lm` with `--wrap` flags for malloc/free).
- **Debugging:** `svc->log_printf()` outputs to USB JTAG (COM30, 115200 baud). Do NOT use `printf()`.
