set(NOFRENDO_SRCS
    bitmap.c
    config.c
    event.c
    gui.c
    gui_elem.c
    intro.c
    log.c
    memguard.c
    nofrendo.c
    pcx.c
    vid_drv.c
    cpu/dis6502.c
    cpu/nes6502.c
    esp32/osd.c
    esp32/video_audio.c
    libsnss/libsnss.c
    mappers/map000.c
    mappers/map001.c
    mappers/map002.c
    mappers/map003.c
    mappers/map004.c
    mappers/map005.c
    mappers/map007.c
    mappers/map008.c
    mappers/map009.c
    mappers/map010.c
    mappers/map011.c
    mappers/map015.c
    mappers/map016.c
    mappers/map018.c
    mappers/map019.c
    mappers/map024.c
    mappers/map032.c
    mappers/map033.c
    mappers/map034.c
    mappers/map040.c
    mappers/map041.c
    mappers/map042.c
    mappers/map046.c
    mappers/map050.c
    mappers/map064.c
    mappers/map065.c
    mappers/map066.c
    mappers/map070.c
    mappers/map073.c
    mappers/map075.c
    mappers/map078.c
    mappers/map079.c
    mappers/map085.c
    mappers/map087.c
    mappers/map093.c
    mappers/map094.c
    mappers/map099.c
    mappers/map160.c
    mappers/map229.c
    mappers/map231.c
    mappers/mapvrc.c
    nes/mmclist.c
    nes/nes.c
    nes/nes_mmc.c
    nes/nes_pal.c
    nes/nes_ppu.c
    nes/nes_rom.c
    nes/nesinput.c
    nes/nesstate.c
    sndhrdw/fds_snd.c
    sndhrdw/mmc5_snd.c
    sndhrdw/nes_apu.c
    sndhrdw/vrcvisnd.c
    nofrendo_run.c
)

idf_component_register(
    SRCS ${NOFRENDO_SRCS}
    INCLUDE_DIRS "include"
    REQUIRES odroid esp_psram esp_timer freertos nvs_flash esp_partition
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
    -Wno-error=stringop-truncation
)

# Enable sound
target_compile_definitions(${COMPONENT_LIB} PRIVATE
    CONFIG_SOUND_ENA=1
)
