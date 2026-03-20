/*
 * Odroid Audio Compatibility Layer for ESP32-P4
 *
 * Wraps the ES8311 codec driver for the ESP32-P4 platform.
 */
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ODROID_VOLUME_LEVEL_COUNT (5)

typedef enum
{
    ODROID_VOLUME_LEVEL0 = 0,
    ODROID_VOLUME_LEVEL1 = 1,
    ODROID_VOLUME_LEVEL2 = 2,
    ODROID_VOLUME_LEVEL3 = 3,
    ODROID_VOLUME_LEVEL4 = 4,

    _ODROID_VOLUME_FILLER = 0x7fffffff
} odroid_volume_level;

/**
 * @brief Initialize the audio subsystem.
 * @param sample_rate Sample rate in Hz (e.g. 16000, 32000)
 */
void odroid_audio_init(int sample_rate);

/**
 * @brief Terminate / silence audio output.
 */
void odroid_audio_terminate(void);

/**
 * @brief Set audio volume (0..ODROID_VOLUME_LEVEL_COUNT-1).
 */
void odroid_audio_volume_set(int volume);

/**
 * @brief Get current volume level.
 */
odroid_volume_level odroid_audio_volume_get(void);

/**
 * @brief Cycle to the next volume level and persist.
 */
void odroid_audio_volume_change(void);

/**
 * @brief Submit a buffer of interleaved stereo 16-bit PCM samples.
 * @param stereoAudioBuffer  Interleaved L/R 16-bit signed PCM
 * @param frameCount         Number of frames (each frame = 2 samples)
 */
void odroid_audio_submit(short *stereoAudioBuffer, int frameCount);

/**
 * @brief Submit silence to clear the DMA buffers.
 */
void odroid_audio_submit_zero(void);

/**
 * @brief Get the current sample rate.
 */
int odroid_audio_sample_rate_get(void);

#ifdef __cplusplus
}
#endif
