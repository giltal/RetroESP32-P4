idf_component_register(
    SRCS
        "cpu.c"
        "debug.c"
        "emu.c"
        "events.c"
        "exports.c"
        "fastmem.c"
        "hw.c"
        "inflate.c"
        "keytable.c"
        "lcd.c"
        "lcdc.c"
        "loader.c"
        "main.c"
        "mem.c"
        "newsound.c"
        "palette.c"
        "path.c"
        "rccmds.c"
        "rcfile.c"
        "rckeys.c"
        "rcvars.c"
        "refresh.c"
        "rtc.c"
        "save.c"
        "sound.c"
        "split.c"
        "gnuboy_run.c"
    INCLUDE_DIRS
        "include"
    REQUIRES
        odroid
        esp_psram
        esp_timer
        freertos
        nvs_flash
        esp_partition
)

# Suppress warnings in legacy gnuboy code
target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wno-error=format
    -Wno-error=pointer-sign
    -Wno-error=maybe-uninitialized
    -Wno-error=unused-variable
    -Wno-error=unused-but-set-variable
    -Wno-error=unused-function
    -Wno-error=implicit-function-declaration
    -Wno-error=int-conversion
    -Wno-error=format-truncation
    -Wno-error=misleading-indentation
    -Wno-error=incompatible-pointer-types
    -Wno-error=char-subscripts
    -Wno-error=array-bounds
)

# Disable minizip (we load uncompressed ROMs from SD card)
# Rename symbols that collide with nofrendo
target_compile_definitions(${COMPONENT_LIB} PRIVATE
    GNUBOY_NO_MINIZIP=1
    IS_LITTLE_ENDIAN=1
    vid_init=gnuboy_vid_init
    cpu=gnuboy_cpu
    rom_load=gnuboy_rom_load
)
