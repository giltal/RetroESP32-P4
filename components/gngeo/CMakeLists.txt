set(GNGEO_SRCS
    # Core emulation
    emu.c
    memory.c
    video.c
    timer.c
    pd4990a.c
    frame_skip.c
    neoboot.c

    # ROM loading & crypto
    roms.c
    neocrypt.c
    unzip.c
    stb_zlib.c
    stb_image.c
    resfile.c
    mame_layer.c

    # State save
    state.c

    # Utilities
    list.c
    gnutil.c
    transpack.c

    # ESP32 platform stubs (replaces screen.c, sound.c, event.c, menu.c, conf.c, messages.c)
    esp32_platform.c

    # M68K CPU (generator68k — table-driven)
    generator68k/cpu68k.c
    generator68k/reg68k.c
    generator68k/gen68k.c
    generator68k/cpu68k-0.c
    generator68k/cpu68k-1.c
    generator68k/cpu68k-2.c
    generator68k/cpu68k-3.c
    generator68k/cpu68k-4.c
    generator68k/cpu68k-5.c
    generator68k/cpu68k-6.c
    generator68k/cpu68k-7.c
    generator68k/cpu68k-8.c
    generator68k/cpu68k-9.c
    generator68k/cpu68k-a.c
    generator68k/cpu68k-b.c
    generator68k/cpu68k-c.c
    generator68k/cpu68k-d.c
    generator68k/cpu68k-e.c
    generator68k/cpu68k-f.c
    generator68k_interf.c

    # Z80 CPU (mamez80 — pure C)
    mamez80/z80.c
    mamez80_interf.c

    # YM2610 sound chip
    ym2610/ym2610.c
    ym2610/2610intf.c
)

idf_component_register(
    SRCS ${GNGEO_SRCS}
    INCLUDE_DIRS "include" "." "generator68k" "mamez80" "ym2610"
    REQUIRES esp_timer freertos odroid app_common
)

# Build with -O2 for performance, suppress common warnings in legacy code
target_compile_options(${COMPONENT_LIB} PRIVATE
    -O2
    -Wno-unused-variable
    -Wno-unused-function
    -Wno-unused-but-set-variable
    -Wno-sign-compare
    -Wno-pointer-sign
    -Wno-maybe-uninitialized
    -Wno-implicit-function-declaration
    -Wno-int-conversion
    -Wno-missing-braces
    -Wno-enum-conversion
    -Wno-old-style-definition
    -Wno-parentheses
    -Wno-format
    -Wno-array-bounds
    -Wno-stringop-overflow
    -Wno-misleading-indentation
    -Wno-return-type
    -Wno-incompatible-pointer-types
    -Wno-discarded-qualifiers
    -Wno-shift-count-overflow
)

# GnGeo build defines
target_compile_definitions(${COMPONENT_LIB} PRIVATE
    USE_MAMEZ80=1
    HAVE_BASENAME=1
    ESP32_PLATFORM=1
    EMBEDDED_FS=1
    ROOTPATH="/sd/roms/neogeo/"
    HAVE_LIBZ=1
    USE_GENERATOR68K=1
)
