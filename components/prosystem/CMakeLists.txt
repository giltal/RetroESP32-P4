set(PROSYSTEM_SRCS
    Bios.c
    Cartridge.c
    Database.c
    Hash.c
    Maria.c
    Memory.c
    Palette.c
    Pokey.c
    ProSystem.c
    Region.c
    Riot.c
    Sally.c
    Tia.c
    prosystem_run.c
)

idf_component_register(
    SRCS ${PROSYSTEM_SRCS}
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
