I would like this project to run on a similar platform of the ESP32-P4 with the following differences:
1. No LCD, instead we have MIPI to HDMI bridge (LT8912 - via the DSI interface, I will supply you with the driver), we will run at 640x480 with frame buffer set at RGB888 (limitation of the LT8912 HDMI bridge device).
2. No touch screen
3. No Battery
4. USB, Audio, SD card, I2C, I2S all exactly the same as on current platform
5. PPA does support copy and scale from RGB565 to RGB888
6. Please notice: SNES and Genesis support the menu and volume buttons from the touch screen, need to find a work around (read item 7), on the browser we support search using the touch screen (need to find a different solution), no GPIO gamepad for now
7. We can support for first stage the PS3 gamepad which has extra L2 and R2 buttons we can map to menu and volume instaed of the touch screen.
I wounder what will be the right strategy for this new project, copy everything to a new folder, or other solution you can suggest since we will have so much code in common.

First stage: write everything you need to this file so we will work with it until we decide how to handle the new project.

---

## Implementation Strategy

### Project Structure: Shared Code with Build-Time Platform Selection

**Recommendation: Single workspace, NOT a fork.** ~95% of code is identical between platforms (all emulator cores, audio, input/gamepad, SD card, NVS, PSRAM app loader, build scripts). Only the display pipeline and a few UI bits differ. A fork would mean maintaining two copies of every emulator fix, every menu change, every new feature.

**Mechanism:** A Kconfig option `CONFIG_HDMI_OUTPUT` (bool, default n) controls platform-specific code paths at compile time. Each app's `sdkconfig.defaults` stays unchanged (LCD build). A parallel `sdkconfig.hdmi.defaults` file sets `CONFIG_HDMI_OUTPUT=y` plus any HDMI-specific settings. A build script flag (`build_all.ps1 -hdmi`) selects which defaults to use.

**Conditional code pattern:**
```c
#ifdef CONFIG_HDMI_OUTPUT
    // HDMI: LT8912, 640×480 RGB888, no rotation
    lt8912_draw_bitmap(x, y, w, h, rgb888_buf);
#else
    // LCD: ST7701, 480×800 portrait, PPA rotate 270°
    st7701_lcd_draw_rgb_bitmap(x, y, w, h, rgb565_buf);
#endif
```

Only a small number of files need `#ifdef` guards — the vast majority of code compiles identically for both platforms.

---

### Display Pipeline Changes

#### Current LCD Pipeline
```
Emulator (various res, RGB565)
  → PPA Stage 1: scale to 320×240 RGB565
  → PPA Stage 2: scale 2× + rotate 270° → 480×640 RGB565
  → ST7701 DPI panel (480×800 portrait, 80px black bars)

Launcher (800×480 RGB565)
  → PPA: rotate 270° only → 480×800
  → ST7701 DPI panel
```

#### New HDMI Pipeline
```
Emulator (various res, RGB565)
  → PPA: scale to 640×480 + convert RGB565→RGB888 (single HW op, NO rotation)
  → LT8912 HDMI bridge (640×480 landscape, RGB888)

Launcher (640×480 RGB888)
  → LT8912 HDMI bridge (direct, no PPA needed)
```

**Key differences:**
- **No rotation** — HDMI is native landscape, same as the logical UI layout
- **RGB888 output** — 3 bytes/pixel instead of 2. PPA handles the 565→888 conversion in hardware during the scale operation
- **640×480 target** — smaller than the current 800×480 effective resolution. Launcher UI needs layout adjustment
- **Framebuffer sizes change:**
  - Launcher FB: 640×480×3 = 921,600 bytes (was 800×480×2 = 768,000)
  - Emulator output: 640×480×3 = 921,600 bytes (was 480×640×2 = 614,400)

#### Emulator Scale Factors (HDMI)

| Emulator | Native Res | Scale X | Scale Y | Output | Notes |
|----------|-----------|---------|---------|--------|-------|
| NES | 256×224 | 2.5× | 2.143× | 640×480 | Fills screen |
| Game Boy | 160×144 | 4.0× | 3.333× | 640×480 | Fills screen |
| Game Gear | 160×144 | 4.0× | 3.333× | 640×480 | Fills screen |
| SMS | 256×192 | 2.5× | 2.5× | 640×480 | Fills screen |
| ColecoVision | 256×192 | 2.5× | 2.5× | 640×480 | Fills screen |
| Atari 7800 | 320×240 | 2.0× | 2.0× | 640×480 | Fills screen |
| Atari 2600 | 320×240 | 2.0× | 2.0× | 640×480 | Fills screen |
| Atari Lynx | 160×102 | 4.0× | 4.706× | 640×480 | Fills screen |
| ZX Spectrum | 320×240 | 2.0× | 2.0× | 640×480 | Fills screen |
| SNES | 256×224 | 2.5× | 2.143× | 640×480 | Fills screen |
| Genesis | 320×224 | 2.0× | 2.143× | 640×480 | Fills screen |
| Atari 800 | 336×240 | 1.905× | 2.0× | 640×480 | Fills screen |
| PC Engine | 256×240 | 2.5× | 2.0× | 640×480 | Fills screen |
| Quake | 320×240 | 2.0× | 2.0× | 640×480 | Fills screen |
| Duke3D | 320×200 | 2.0× | 2.4× | 640×480 | Fills screen |
| OpenTyrian | 320×200 | 2.0× | 2.4× | 640×480 | Fills screen |

**Good news:** Every emulator fills the full 640×480 with integer or near-integer scaling. No black bars needed (unlike the current 80px bars on LCD). The two-stage pipeline (scale to 320×240, then scale 2×) can be simplified to a single PPA operation directly from native res to 640×480 RGB888.

#### Simplified Emulator Display Function (HDMI)

The current two-stage approach (scale to `s_emu_scaled` 320×240, then `display_emu_flush_320x240()` with 2× + rotate) becomes a single-stage:

```c
// HDMI: single PPA op — scale native to 640×480 + RGB565→RGB888
void display_emu_flush_hdmi(uint16_t *src, int src_w, int src_h) {
    float sx = 640.0f / src_w;
    float sy = 480.0f / src_h;
    ppa_scale_rgb565_to_rgb888(src, src_w, src_h, s_hdmi_buf, 640, 480, sx, sy);
    lt8912_draw_bitmap(0, 0, 640, 480, s_hdmi_buf);
}
```

This eliminates the intermediate `s_emu_scaled` buffer and the rotation entirely. Each emulator can pass its native resolution buffer directly.

---

### Launcher UI Layout Changes

Current launcher targets 800×480 (landscape after rotation). HDMI targets 640×480.

| Element | LCD (800×480) | HDMI (640×480) |
|---------|--------------|----------------|
| Framebuffer | 800×480 RGB565 | 640×480 RGB888 |
| Font rendering | 8×16 at 2× = 16×32 | 8×16 at 2× = 16×32 (same) |
| Carousel icons | 32×32 (2× scaled) | 32×32 (same) |
| System artwork | 400×225 PNG → 800×450 | 320×225 PNG → 640×450 (or reuse with crop/scale) |
| Browser rows | ~10 rows, 44px each | ~10 rows, 44px each (same, slightly tighter) |
| Header bar | 800px wide | 640px wide |
| Status icons | Right-aligned at x=736,696,... | Right-aligned at x=576,536,... |
| PAPP preview | 300×300 right-aligned | ~240×240 right-aligned (less space) |

Most layout code uses `SCREEN.w` and `SCREEN.h` — if these are set to 640/480, many things adapt automatically. The main work is adjusting hardcoded pixel positions and ensuring nothing overflows 640px width.

**Pixel format change (RGB565→RGB888):** All `draw_text()`, `fill_rect()`, `draw_icon()` functions currently write `uint16_t` RGB565 values with stride calculations based on 2 bytes/pixel. For HDMI these need to write `uint8_t[3]` RGB888 triplets. This is the most pervasive change in the launcher. Options:
- (A) **Dual-mode helpers** — `#ifdef` inside each draw function to handle both formats
- (B) **Internal RGB565 + convert on flush** — launcher draws to an internal 640×480 RGB565 buffer, PPA converts to RGB888 on flush. Simpler code, one extra PPA step, ~1-2ms overhead
- (C) **Native RGB888 everywhere** — rewrite all draw functions for RGB888

**Recommendation: Option B** — Keep all drawing code as RGB565 (zero changes to existing draw functions), add a PPA RGB565→RGB888 conversion step in `display_flush()` for HDMI. This minimizes code churn and keeps the two platforms maximally shared. The PPA conversion is fast (<2ms for 640×480).

---

### Input Changes

#### Remove Touch-Based Features
- **SNES/Genesis sidebar MENU/VOL buttons** — Currently rendered in the LCD's black side bars and triggered by touch zones. HDMI has no side bars and no touch. Remove sidebar rendering entirely under `CONFIG_HDMI_OUTPUT`.
- **Launcher touch search keyboard** — Replace with a **button-driven letter picker**: L/R to scroll through alphabet, A to select letter, B to delete, Start to confirm. Simpler but functional.
- **Touch-zone MENU/VOLUME mapping** — Entirely removed (no `s_touch_menu`/`s_touch_volume`).

#### PS3 L2/R2 → MENU/VOLUME Mapping
The PS3 DualShock 3 report parser already detects L2/R2 as digital buttons (byte 3, bits 0-1). Map:
- **L2 → ODROID_INPUT_MENU** (opens in-game menu in all emulators)
- **R2 → ODROID_INPUT_VOLUME** (cycles volume)

This is a simple addition to `odroid_input_gamepad_read()`:
```c
#ifdef CONFIG_HDMI_OUTPUT
    // HDMI platform: L2→Menu, R2→Volume (no touch screen)
    if (state.values[ODROID_INPUT_L2]) state.values[ODROID_INPUT_MENU] = 1;
    if (state.values[ODROID_INPUT_R2]) state.values[ODROID_INPUT_VOLUME] = 1;
#endif
```

This works for ALL controllers (not just PS3) since the USB mapping system already maps physical L2/R2 to `ODROID_INPUT_L2`/`ODROID_INPUT_R2`.

#### No GPIO Gamepad
Guard all GPIO gamepad code with `#ifndef CONFIG_HDMI_OUTPUT` (or `#ifdef CONFIG_GPIO_GAMEPAD`). The ADC2 channels, detection logic, and GPIO pin setup are all skipped.

---

### Battery Removal
- `odroid_input_battery_level_init()` / `_read()` — no-op or skipped under `CONFIG_HDMI_OUTPUT`
- Launcher `draw_battery()` — skipped, icon slot reclaimed or left empty
- `odroid_battery_state.charging` — unused

---

### New Component: LT8912 HDMI Bridge Driver

Parallel to `components/st7701_lcd/`:
```
components/lt8912_hdmi/
    CMakeLists.txt
    lt8912_hdmi.c          ← I2C init, MIPI DSI lane config, HDMI setup
    include/lt8912_hdmi.h  ← Public API
```

**API (mirrors st7701_lcd pattern):**
```c
esp_err_t lt8912_hdmi_init(void);                              // I2C + DSI + HDMI init
esp_err_t lt8912_hdmi_draw_bitmap(int x, int y, int w, int h,
                                   const uint8_t *rgb888_buf); // Push RGB888 frame
```

The LT8912 communicates via I2C for register configuration and receives pixel data via MIPI DSI. The DSI interface will be configured for 640×480 RGB888 with appropriate timing parameters (which you'll provide with the driver).

---

### PPA Engine Extension

Need to verify/add a PPA function that does **scale + RGB565→RGB888 conversion** in a single hardware operation. Current PPA functions only handle RGB565→RGB565.

Check ESP-IDF PPA API for:
- `PPA_SRM_COLOR_MODE_RGB888` output support
- Whether scale + color conversion can be combined in one SRM operation

If PPA doesn't support RGB565→RGB888 directly, fallback: PPA scale (RGB565→RGB565) + CPU color convert (fast loop: extract R5G6B5 → R8G8B8, ~2ms for 640×480).

### PPA RGB565→RGB888 Verification — CONFIRMED ✅

**ESP-IDF v5.5.2 PPA SRM fully supports mixed input/output color modes.**

From `hal/ppa_types.h`, the `ppa_srm_color_mode_t` enum includes both:
- `PPA_SRM_COLOR_MODE_RGB565` — for input
- `PPA_SRM_COLOR_MODE_RGB888` — for output

The SRM operation config (`ppa_srm_oper_config_t`) has independent `in.srm_cm` and `out.srm_cm` fields. The driver validates each independently with `ppa_ll_srm_is_color_mode_supported()` — there is **no restriction** against mixing different color modes between input and output.

**Scale + color convert in one PPA operation: YES.** A single `ppa_do_scale_rotate_mirror()` call can:
- Input: RGB565 emulator framebuffer
- Scale X/Y to 640×480
- Convert to RGB888 output
- All in one hardware pass

**BGR888 byte order:** The LT8912 DPI panel expects BGR888 (`{B, G, R}` per pixel). PPA has an `rgb_swap` flag on the **input** side that swaps RGB↔BGR. For RGB565 input with RGB888 output:
- `rgb_swap = true` on input: swaps R and B channels in the RGB565 before processing
- PPA then outputs as RGB888 but with channels swapped → effectively BGR888
- This needs testing to confirm the swap applies correctly across the format conversion

**If `rgb_swap` doesn't produce correct BGR888 output**, the alternative is trivially cheap: PPA outputs RGB888 (correct scale + format convert), then a simple CPU loop swaps R↔B bytes in the output buffer. For 640×480×3 = 921,600 bytes, this is ~1ms.

**Usage pattern for HDMI emulator display:**
```c
ppa_srm_oper_config_t srm = {
    .in = {
        .buffer = emu_rgb565_buf,
        .pic_w = native_w, .pic_h = native_h,
        .block_w = native_w, .block_h = native_h,
        .block_offset_x = 0, .block_offset_y = 0,
        .srm_cm = PPA_SRM_COLOR_MODE_RGB565,
    },
    .out = {
        .buffer = hdmi_fb,              // DPI panel framebuffer (BGR888)
        .buffer_size = 640 * 480 * 3,
        .pic_w = 640, .pic_h = 480,
        .block_offset_x = 0, .block_offset_y = 0,
        .srm_cm = PPA_SRM_COLOR_MODE_RGB888,
    },
    .scale_x = 640.0f / native_w,
    .scale_y = 480.0f / native_h,
    .rotation_angle = PPA_SRM_ROTATION_ANGLE_0,  // No rotation for HDMI!
    .rgb_swap = true,   // RGB565→BGR888 (needs testing)
    .mode = PPA_TRANS_MODE_BLOCKING,
};
ppa_do_scale_rotate_mirror(srm_client, &srm);
esp_cache_msync(hdmi_fb, 640*480*3, ESP_CACHE_MSYNC_FLAG_DIR_C2M);
```

---

### Files That Need `#ifdef CONFIG_HDMI_OUTPUT` Changes

| File | Changes |
|------|---------|
| `components/odroid/odroid_display.c` | Display init, flush functions, buffer allocations, scale factors |
| `components/odroid/odroid_display.h` | `FB_W`/`FB_H` constants (640/480), buffer types |
| `components/odroid/odroid_system.c` | Init LT8912 instead of ST7701 |
| `components/odroid/odroid_input.c` | Skip touch, skip GPIO pad, L2/R2→MENU/VOL mapping |
| `components/odroid/include/odroid_input.h` | Skip battery/touch declarations |
| `launcher/main/main.c` | Layout adjustments, skip touch keyboard, skip battery icon, button-driven search |
| `apps/snes/main/snes_run.c` | Skip sidebar button rendering |
| `apps/genesis/main/genesis_run.c` | Skip sidebar button rendering |
| `components/ppa_engine/ppa_engine.c` | Add RGB565→RGB888 scale function |
| `main/pins_config.h` | LT8912 I2C pins (if different) |

**Files unchanged (vast majority):** All emulator cores, audio, SD card, NVS, gamepad parser, PSRAM app loader, save/load logic, menu rendering, font data, build scripts (aside from adding `-hdmi` flag).

---

### Build System

```
# LCD build (default, unchanged)
.\build_all.ps1

# HDMI build
.\build_all.ps1 -hdmi
```

The `-hdmi` flag causes `build_all.ps1` to:
1. Copy `sdkconfig.hdmi.defaults` → `sdkconfig.defaults` before each `idf.py build`
2. Set output directory to `firmware_hdmi/` to keep LCD and HDMI binaries separate
3. Generate `RetroESP32_P4_HDMI_v1.bin` merged firmware

Each app's `sdkconfig.hdmi.defaults` includes:
```
CONFIG_HDMI_OUTPUT=y
# Plus all the existing settings from sdkconfig.defaults
```

---

### Implementation Order

1. **Create LT8912 component** — driver init + draw_bitmap API (you provide the driver, I integrate)
2. **Add Kconfig option** — `CONFIG_HDMI_OUTPUT` bool in root/component Kconfig
3. **PPA RGB565→RGB888** — verify/implement scale+convert function
4. **odroid_display.c** — HDMI display pipeline (scale to 640×480 RGB888, no rotation)
5. **odroid_system.c** — conditional init (LT8912 vs ST7701)
6. **odroid_input.c** — L2/R2 mapping, skip touch/GPIO/battery
7. **Launcher UI** — adjust layout for 640×480, button-based search, skip battery icon
8. **SNES/Genesis** — remove sidebar buttons under HDMI
9. **PSRAM apps** — update display_write_frame_custom for HDMI output
10. **Build scripts** — add `-hdmi` flag
11. **Test** — build + flash HDMI variant, verify all emulators

---

### Open Questions — Resolved

| # | Question | Answer |
|---|----------|--------|
| 1 | LT8912 driver source? | Provided: `components/lt8912/` (I2C register driver) + `components/hdmi_display/` (DSI+DPI init wrapper). Call `hdmi_display_init()` to get a raw framebuffer pointer. |
| 2 | MIPI DSI timing? | Covered by driver: 2 lanes, 1000 Mbps, 40 MHz pixel clock. 640×480: htotal=1270, vtotal=525. |
| 3 | PPA RGB888 support? | **Still need to verify** — ESP-IDF v5.5.2 PPA SRM output color mode. |
| 4 | Audio? | Same I2S/ES8311 codec. No HDMI audio. |
| 5 | Pin changes? | None — identical platform, just LCD+touch replaced by LT8912 HDMI bridge. Same I2C bus (SDA=7, SCL=8). |
| 6 | PSRAM apps (.papp)? | Service table's `display_write_frame_custom()` needs HDMI-aware path. |

---

### Driver Architecture (from provided components)

The display init is much simpler than originally planned. The `hdmi_display` component handles everything:

```c
hdmi_display_t disp;
hdmi_display_init(HDMI_MODE_640x480, &disp);
// disp.fb    → raw framebuffer in PSRAM (640×480×3 = 921,600 bytes, BGR888)
// disp.fb_size = 921600
// Write pixels directly, then:
esp_cache_msync(disp.fb, disp.fb_size, ESP_CACHE_MSYNC_FLAG_DIR_C2M);
// Screen updates on next DPI refresh (~60Hz continuous)
```

**Key details from the driver:**
- **Pixel format:** BGR888 byte order (Red = `{0x00, 0x00, 0xFF}`)
- **Framebuffer:** DPI panel-managed, PSRAM-backed, single-buffer
- **Refresh:** Continuous DPI streaming — just write to FB + cache sync, no explicit "push frame" call needed
- **I2C pins:** From Kconfig: `CONFIG_LT8912_I2C_SDA`, `CONFIG_LT8912_I2C_SCL`, `CONFIG_LT8912_RESET_GPIO`
- **I2C bus:** Creates its own `i2c_new_master_bus()` on `I2C_NUM_0` — **potential conflict** with existing `odroid_system.c` which also inits I2C for the ES8311 codec and GT911 touch. Under HDMI build, the I2C bus init must happen only once and the handle shared, or `hdmi_display` must init first and the audio codec reuses the bus.

### Revised Display Pipeline (HDMI)

Since the HDMI driver gives us a raw memory-mapped framebuffer with continuous DPI refresh, the pipeline is:

```
Emulator (various res, RGB565)
  → PPA: scale to 640×480 + convert RGB565→BGR888 (if PPA supports it)
     OR: PPA scale to 640×480 RGB565, then CPU convert RGB565→BGR888
  → memcpy/write directly to disp.fb
  → esp_cache_msync() → appears on screen

Launcher (640×480, internal RGB565 drawing)
  → PPA or CPU convert RGB565→BGR888 into disp.fb
  → esp_cache_msync()
```

**No `draw_bitmap()` call needed** — the framebuffer is always live. Just write and sync cache. This is simpler than the ST7701 path which needed `st7701_lcd_draw_rgb_bitmap()`.

### Color Format Note: BGR888

The LT8912 DPI panel uses **BGR888 byte order**. RGB565→BGR888 conversion for a pixel:
```c
// RGB565: RRRRR GGGGGG BBBBB (uint16_t)
// BGR888: [B8, G8, R8] (3 bytes)
uint16_t px = src_rgb565;
uint8_t r5 = (px >> 11) & 0x1F;
uint8_t g6 = (px >> 5)  & 0x3F;
uint8_t b5 =  px        & 0x1F;
dst[0] = (b5 << 3) | (b5 >> 2);  // B8
dst[1] = (g6 << 2) | (g6 >> 4);  // G8
dst[2] = (r5 << 3) | (r5 >> 2);  // R8
```

If PPA SRM supports `PPA_SRM_COLOR_MODE_RGB888` output, it may handle this in hardware. If not, CPU conversion at ~640×480 = 307,200 pixels should take ~2-3ms (acceptable at 60Hz).

