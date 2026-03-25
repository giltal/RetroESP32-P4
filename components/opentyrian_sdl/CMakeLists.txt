idf_component_register(
    SRCS
        "SDL_video.c"
        "SDL_event.c"
        "SDL_audio.c"
        "SDL_system.c"
    INCLUDE_DIRS "."
    REQUIRES app_common odroid audio esp_timer
)

# Suppress warnings from SDL compatibility code
target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wno-pointer-sign
    -Wno-int-conversion
    -Wno-incompatible-pointer-types
)
