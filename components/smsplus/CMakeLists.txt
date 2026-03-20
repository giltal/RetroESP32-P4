set(SMSPLUS_SRCS
    config.c
    crc32.c
    error.c
    fileio.c
    hvc.c
    loadrom.c
    memz80.c
    pio.c
    render.c
    render_bp_lut.c
    render_lut.c
    sms.c
    state.c
    system.c
    tms.c
    tms_bp_expand.c
    tms_lookup.c
    tms_mc_lookup.c
    tms_obj_lut.c
    tms_txt_lookup.c
    vdp.c
    cpu/z80.c
    cpu/z80_SZHVC_add_table.c
    cpu/z80_SZHVC_sub_table.c
    sound/emu2413.c
    sound/fmintf.c
    sound/sn76489.c
    sound/sound.c
    sound/ym2413.c
    smsplus_run.c
)

idf_component_register(
    SRCS ${SMSPLUS_SRCS}
    INCLUDE_DIRS "include"
    REQUIRES odroid esp_psram esp_timer freertos nvs_flash
)

# Suppress warnings from legacy C code
target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wno-error=unused-variable
    -Wno-error=unused-but-set-variable
    -Wno-error=unused-function
    -Wno-error=unused-value
    -Wno-error=implicit-function-declaration
    -Wno-error=incompatible-pointer-types
    -Wno-error=pointer-sign
    -Wno-error=int-conversion
    -Wno-error=format
    -Wno-error=maybe-uninitialized
    -Wno-error=char-subscripts
    -Wno-error=missing-braces
    -Wno-error=switch
    -Wno-error=discarded-qualifiers
    -Wno-error=implicit-int
    -Wno-error=shadow
    -Wno-error=sign-compare
    -Wno-error=old-style-definition
    -Wno-error=missing-declarations
    -Wno-error=missing-prototypes
    -Wno-error=parentheses
    -Wno-error=misleading-indentation
    -Wno-error=unused-const-variable
    -Wno-error=address-of-packed-member
)

# Rename conflicting symbols with gnuboy
target_compile_definitions(${COMPONENT_LIB} PRIVATE
    snd=smsplus_snd
    sound_reset=smsplus_sound_reset
)
