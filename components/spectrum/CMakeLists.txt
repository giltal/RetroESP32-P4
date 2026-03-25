idf_component_register(
    SRCS
        "ay_sound.c"
        "compr.c"
        "interf.c"
        "keynames.c"
        "loadim.c"
        "misc.c"
        "rom_imag.c"
        "snapshot.c"
        "spconf.c"
        "spect.c"
        "spectrum_run.c"
        "spkey.c"
        "spmain.c"
        "spperif.c"
        "spscr.c"
        "spsound.c"
        "sptape.c"
        "sptiming.c"
        "stubs.c"
        "tapefile.c"
        "vgakey.c"
        "vgascr.c"
        "z80.c"
        "z80optab.c"
        "z80_op1.c"
        "z80_op2.c"
        "z80_op3.c"
        "z80_op4.c"
        "z80_op5.c"
        "z80_op6.c"
        "z80_step.c"
    INCLUDE_DIRS
        "include"
    REQUIRES
        odroid
        esp_timer
        freertos
        nvs_flash
        esp_psram
)

# Key defines that config.h provides — needed by z80.h and spperif.h
target_compile_definitions(${COMPONENT_LIB} PUBLIC
    SPECT_MEM=1
    Z80C=1
    HAVE_SOUND=1
)

# Force -O3 for maximum emulation speed
target_compile_options(${COMPONENT_LIB} PRIVATE -O3)

# Suppress warnings in legacy C code
target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wno-error
    -Wno-unused-variable
    -Wno-unused-function
    -Wno-unused-but-set-variable
    -Wno-maybe-uninitialized
    -Wno-implicit-function-declaration
    -Wno-int-conversion
    -Wno-incompatible-pointer-types
    -Wno-pointer-sign
    -Wno-sign-compare
    -Wno-missing-braces
    -Wno-old-style-definition
    -Wno-parentheses
    -Wno-format
    -Wno-misleading-indentation
    -Wno-char-subscripts
)
