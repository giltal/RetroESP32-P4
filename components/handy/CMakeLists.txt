idf_component_register(
    SRCS
        "handy_run.cpp"
        "cart.cpp"
        "eeprom.cpp"
        "lynxdec.cpp"
        "memmap.cpp"
        "mikie.cpp"
        "ram.cpp"
        "rom.cpp"
        "susie.cpp"
        "system.cpp"
    INCLUDE_DIRS "."
    REQUIRES odroid esp_timer
)

# C++ flags: no exceptions, no RTTI, optimise for size
target_compile_options(${COMPONENT_LIB} PRIVATE
    -fno-exceptions
    -fno-rtti
    -Os
    -Wno-narrowing
    -Wno-write-strings
    -Wno-unused-variable
    -Wno-sign-compare
    -Wno-parentheses
    -Wno-format
    -Wno-unused-but-set-variable
    -Wno-comment
    -Wno-sequence-point
    -Wno-sizeof-pointer-memaccess
    -Wno-maybe-uninitialized
)

# Defines matching the original RetroESP32 build
target_compile_definitions(${COMPONENT_LIB} PRIVATE
    MY_NO_STATIC
    MY_KEYS_IN_VIDEO
    MY_RETRO_LOOP
    MY_SYSTEM_LOOP=2
    MY_MEM_MODE_V2
    MY_AUDIO_MODE=2
    MY_ALLOC
    GIT_VERSION=""
)
