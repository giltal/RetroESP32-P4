/*
 * Audio driver for ES8311 codec via I2S
 *
 * Uses esp_codec_dev to configure the ES8311 over I2C and stream
 * audio data through the ESP32-P4 I2S peripheral.
 */
#pragma once

#include "esp_err.h"
#include "driver/i2c_master.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Audio configuration
 */
typedef struct {
    int i2s_num;           /*!< I2S port number (0 or 1) */
    int mclk_io;           /*!< MCLK GPIO */
    int bclk_io;           /*!< BCLK (SCLK) GPIO */
    int ws_io;             /*!< WS (LRCK) GPIO */
    int dout_io;           /*!< Data Out GPIO (to ES8311 SDIN) */
    int din_io;            /*!< Data In GPIO (from ES8311 DOUT) */
    int pa_ctrl_io;        /*!< Power amplifier control GPIO (-1 to disable) */
    int sample_rate;       /*!< Sample rate in Hz */
    int volume;            /*!< Initial volume 0-100 */
    i2c_master_bus_handle_t i2c_handle; /*!< Existing I2C bus handle (shared with touch) */
} audio_config_t;

/**
 * @brief Default audio config for the ESP32-P4 platform
 */
#define AUDIO_CONFIG_DEFAULT() { \
    .i2s_num = 0,               \
    .mclk_io = 13,              \
    .bclk_io = 12,              \
    .ws_io = 10,                \
    .dout_io = 9,               \
    .din_io = 48,               \
    .pa_ctrl_io = -1,           \
    .sample_rate = 16000,       \
    .volume = 60,               \
    .i2c_handle = NULL,         \
}

/**
 * @brief Initialize the audio subsystem (I2S + ES8311 codec)
 *
 * @param config Audio configuration with pin assignments and I2C bus handle
 * @return ESP_OK on success
 */
esp_err_t audio_init(const audio_config_t *config);

/**
 * @brief Play a sine wave tone through the speaker
 *
 * @param freq_hz     Tone frequency in Hz (e.g. 440 for A4)
 * @param duration_ms Duration in milliseconds (0 = play indefinitely)
 * @param volume      Volume 0-100 (overrides current volume)
 * @return ESP_OK on success
 */
esp_err_t audio_play_tone(uint32_t freq_hz, uint32_t duration_ms, int volume);

/**
 * @brief Stop any currently playing audio
 */
esp_err_t audio_stop(void);

/**
 * @brief Set the output volume
 *
 * @param volume Volume 0-100
 * @return ESP_OK on success
 */
esp_err_t audio_set_volume(int volume);

/**
 * @brief Reconfigure the I2S clock to a new sample rate at runtime
 *
 * @param sample_rate New sample rate in Hz (e.g. 32000)
 * @return ESP_OK on success
 */
esp_err_t audio_set_sample_rate(int sample_rate);

/**
 * @brief Play a PCM buffer through the speaker
 *
 * @param data      Pointer to 16-bit signed PCM data (stereo interleaved)
 * @param len       Length of data in bytes
 * @param sample_rate Sample rate of the PCM data
 * @return ESP_OK on success
 */
esp_err_t audio_play_pcm(const void *data, size_t len, int sample_rate);

#ifdef __cplusplus
}
#endif
