# HuExpress – PC Engine / TurboGrafx-16 emulator component
# Engine core + platform OSD stubs + run glue layer

set(SRCS
    # Engine core
    engine/bios.c
    engine/bp.c
    engine/cd.c
    engine/cheat.c
    engine/debug.c
    engine/dis.c
    engine/edit_ram.c
    engine/followop.c
    engine/format.c
    engine/gfx.c
    engine/h6280.c
    engine/hard_pce.c
    engine/hcd.c
    engine/lsmp3.c
    engine/mix.c
    engine/ogglength.c
    engine/optable.c
    engine/pce.c
    engine/pcecd.c
    engine/romdb.c
    engine/sound.c
    engine/sprite.c
    engine/subs_eagle.c
    engine/trans_fx.c
    # Root-level support files (portable)
    iniconfig.c
    utils.c
    osd_dummy_cd.c
    # Glue layer — implements ALL OSD functions for ESP32-P4
    huexpress_run.c
)

idf_component_register(
    SRCS ${SRCS}
    INCLUDE_DIRS "." "includes" "engine" "include"
    REQUIRES odroid esp_psram esp_timer freertos nvs_flash
)

# Suppress warnings from the vintage C codebase
target_compile_options(${COMPONENT_LIB} PRIVATE
    -DLSB_FIRST=1
    -Wno-all
    -Wno-error
    -Wno-unused-function
    -Wno-unused-variable
    -Wno-unused-but-set-variable
    -Wno-sign-compare
    -Wno-implicit-function-declaration
    -Wno-incompatible-pointer-types
    -Wno-int-conversion
    -Wno-pointer-sign
    -Wno-missing-braces
    -Wno-maybe-uninitialized
    -Wno-format
    -Wno-parentheses
    -Wno-implicit-fallthrough
    -Wno-misleading-indentation
    -Wno-comment
    -fgnu89-inline
)
