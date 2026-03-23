idf_component_register(
    SRC_DIRS
        "src/bus"
        "src/cpus/M68K"
        "src/cpus/Z80"
        "src/io"
        "src/savestate"
        "src/sound"
        "src/vdp"
    INCLUDE_DIRS
        "src/bus"
        "src/cpus/M68K"
        "src/cpus/Z80"
        "src/io"
        "src/savestate"
        "src/sound"
        "src/vdp"
)

target_compile_options(${COMPONENT_LIB} PRIVATE -O3 -Wno-unused-const-variable -Wno-unused-value -Wno-char-subscripts -Wno-implicit-fallthrough -Wno-unused-function -Wno-unused-variable)
target_compile_definitions(${COMPONENT_LIB} PRIVATE GWENESIS_AUDIO_ACCURATE=0)
