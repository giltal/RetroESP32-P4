# CMakeLists.txt for atari800 (libatari800) component
# Ported from component.mk for ESP-IDF CMake build system

set(ATARI800_CORE_SRCS
    afile.cpp
    antic.cpp
    artifact.cpp
    atari.cpp
    binload.cpp
    cartridge.cpp
    cassette.cpp
    cfg.cpp
    compfile.cpp
    cpu.cpp
    crc32.cpp
    devices.cpp
    esc.cpp
    gtia.cpp
    img_tape.cpp
    input.cpp
    libatari800_exit.cpp
    libatari800_init.cpp
    libatari800_main.cpp
    libatari800_sound.cpp
    libatari800_statesav.cpp
    libatari800_video.cpp
    log.cpp
    memory.cpp
    pbi.cpp
    pia.cpp
    pokey.cpp
    pokeysnd.cpp
    roms_altirra_5200_os.cpp
    roms_altirra_basic.cpp
    roms_altirraos_800.cpp
    roms_altirraos_xl.cpp
    screen.cpp
    sio.cpp
    sndsave.cpp
    sound.cpp
    statesav.cpp
    sysrom.cpp
    util.cpp
    atari800_run.cpp
)

idf_component_register(
    SRCS ${ATARI800_CORE_SRCS}
    INCLUDE_DIRS "." "include"
    REQUIRES odroid esp_psram esp_timer freertos nvs_flash
)

# Suppress warnings from legacy C++ code
target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wno-error=format
    -Wno-error=maybe-uninitialized
    -Wno-error=unused-variable
    -Wno-error=unused-but-set-variable
    -Wno-error=write-strings
    -Wno-error=narrowing
    -Wno-error=sign-compare
    -Wno-error=implicit-fallthrough
    -Wno-error=parentheses
    -Wno-error=misleading-indentation
    -Wno-error=char-subscripts
    -Wno-error=address
    -Wno-error=type-limits
    -Wno-error=pointer-sign
    -Wno-error=int-to-pointer-cast
    -Wno-format
    -Wno-maybe-uninitialized
    -Wno-unused-variable
    -Wno-unused-but-set-variable
    -Wno-write-strings
    -Wno-narrowing
    -Wno-sign-compare
    -Wno-implicit-fallthrough
    -Wno-parentheses
    -Wno-misleading-indentation
    -Wno-char-subscripts
    -Wno-address
    -Wno-type-limits
    -Wno-pointer-sign
    -Wno-int-to-pointer-cast
    -Wno-array-bounds
    -Wno-error=array-bounds
    -Wno-stringop-overread
    -Wno-error=stringop-overread
    -Wno-stringop-overflow
    -Wno-error=stringop-overflow
)
