# snes9x emulation core for RetroESP32-P4
set(SNES9X_SRCS
    spc700.c
    memmap.c
    apu.c
    globals.c
    c4emu.c
    cpuops.c
    dsp1emu.c
    srtc.c
    apu_blargg.c
    clip.c
    cpu.c
    gfx.c
    soundux.c
    cpuexec.c
    obc1.c
    dsp2emu.c
    tile.c
    ppu.c
    dma.c
    save.c
    c4.c
    dsp1.c
    getset.c
    fxemu.c
    fxinst.c
)

idf_component_register(
    SRCS ${SNES9X_SRCS}
    INCLUDE_DIRS "." "./.."
    REQUIRES odroid esp_psram esp_timer freertos esp_partition
)

# Suppress warnings from legacy C code
target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wno-error=format
    -Wno-error=unused-const-variable
    -Wno-error=incompatible-pointer-types
    -Wno-error=unused-variable
    -Wno-error=unused-but-set-variable
    -Wno-error=pointer-sign
    -Wno-error=int-conversion
    -Wno-error=missing-braces
    -Wno-error=sign-compare
    -Wno-error=implicit-function-declaration
    -Wno-error=missing-prototypes
    -Wno-error=missing-declarations
    -Wno-error=old-style-definition
    -Wno-error=maybe-uninitialized
    -Wno-error=implicit-int
    -O3
)

# Use inline COLOR_SUB1_2 instead of GFX.ZERO lookup table (which is not
# allocated on ESP32). Without this, DKC colour-math effects dereference NULL.
target_compile_definitions(${COMPONENT_LIB} PUBLIC NO_ZERO_LUT)
