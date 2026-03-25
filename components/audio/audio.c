/*
 * Audio driver for ES8311 codec via I2S
 *
 * Configures the I2S peripheral in standard (Philips) mode and uses
 * esp_codec_dev to set up the ES8311 over the shared I2C bus.
 */

#include "audio.h"
#include <math.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2s_std.h"
#include "driver/gpio.h"
#include "esp_codec_dev_defaults.h"
#include "esp_codec_dev.h"
#include "esp_codec_dev_vol.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_heap_caps.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static const char *TAG = "audio";

/* MCLK multiple — 256 is sufficient for 16-bit; use 384 for 24-bit */
#define MCLK_MULTIPLE   (256)

/* I2S handles */
static i2s_chan_handle_t s_tx_handle = NULL;
static i2s_chan_handle_t s_rx_handle = NULL;

/* Codec device handle */
static esp_codec_dev_handle_t s_codec_handle = NULL;

/* Current config */
static audio_config_t s_config;
static bool s_initialized = false;

/* ─── I2S driver init ─────────────────────────────────────────────── */
static esp_err_t i2s_driver_init(const audio_config_t *cfg)
{
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG((i2s_port_t)cfg->i2s_num, I2S_ROLE_MASTER);
    chan_cfg.auto_clear = true;
    ESP_RETURN_ON_ERROR(i2s_new_channel(&chan_cfg, &s_tx_handle, &s_rx_handle), TAG, "i2s_new_channel failed");

    i2s_std_config_t std_cfg = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(cfg->sample_rate),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = (gpio_num_t)cfg->mclk_io,
            .bclk = (gpio_num_t)cfg->bclk_io,
            .ws   = (gpio_num_t)cfg->ws_io,
            .dout = (gpio_num_t)cfg->dout_io,
            .din  = (gpio_num_t)cfg->din_io,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv   = false,
            },
        },
    };
    std_cfg.clk_cfg.mclk_multiple = (i2s_mclk_multiple_t)MCLK_MULTIPLE;

    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(s_tx_handle, &std_cfg), TAG, "TX init failed");
    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(s_rx_handle, &std_cfg), TAG, "RX init failed");
    ESP_RETURN_ON_ERROR(i2s_channel_enable(s_tx_handle), TAG, "TX enable failed");
    ESP_RETURN_ON_ERROR(i2s_channel_enable(s_rx_handle), TAG, "RX enable failed");

    ESP_LOGI(TAG, "I2S%d initialized: SR=%d, MCLK=%d, BCLK=%d, WS=%d, DOUT=%d, DIN=%d",
             cfg->i2s_num, cfg->sample_rate,
             cfg->mclk_io, cfg->bclk_io, cfg->ws_io, cfg->dout_io, cfg->din_io);
    return ESP_OK;
}

/* ─── ES8311 codec init via esp_codec_dev ─────────────────────────── */
static esp_err_t es8311_codec_init(const audio_config_t *cfg)
{
    /* Create I2C control interface using the shared bus handle */
    audio_codec_i2c_cfg_t i2c_cfg = {
        .port = 0,   /* not used when bus_handle is provided */
        .addr = ES8311_CODEC_DEFAULT_ADDR,
        .bus_handle = cfg->i2c_handle,
    };
    const audio_codec_ctrl_if_t *ctrl_if = audio_codec_new_i2c_ctrl(&i2c_cfg);
    if (!ctrl_if) {
        ESP_LOGE(TAG, "Failed to create I2C control interface");
        return ESP_FAIL;
    }

    /* Create I2S data interface */
    audio_codec_i2s_cfg_t i2s_cfg = {
        .port = cfg->i2s_num,
        .rx_handle = s_rx_handle,
        .tx_handle = s_tx_handle,
    };
    const audio_codec_data_if_t *data_if = audio_codec_new_i2s_data(&i2s_cfg);
    if (!data_if) {
        ESP_LOGE(TAG, "Failed to create I2S data interface");
        return ESP_FAIL;
    }

    /* Create GPIO interface */
    const audio_codec_gpio_if_t *gpio_if = audio_codec_new_gpio();
    if (!gpio_if) {
        ESP_LOGE(TAG, "Failed to create GPIO interface");
        return ESP_FAIL;
    }

    /* Create ES8311 codec interface */
    es8311_codec_cfg_t es8311_cfg = {
        .ctrl_if    = ctrl_if,
        .gpio_if    = gpio_if,
        .codec_mode = ESP_CODEC_DEV_WORK_MODE_BOTH,
        .master_mode = false,
        .use_mclk   = cfg->mclk_io >= 0,
        .pa_pin      = (int16_t)cfg->pa_ctrl_io,
        .pa_reverted = false,
        .hw_gain = {
            .pa_voltage = 5.0,
            .codec_dac_voltage = 3.3,
        },
        .mclk_div = MCLK_MULTIPLE,
    };
    const audio_codec_if_t *es8311_if = es8311_codec_new(&es8311_cfg);
    if (!es8311_if) {
        ESP_LOGE(TAG, "Failed to create ES8311 codec interface");
        return ESP_FAIL;
    }

    /* Create top-level codec device */
    esp_codec_dev_cfg_t dev_cfg = {
        .dev_type = ESP_CODEC_DEV_TYPE_IN_OUT,
        .codec_if = es8311_if,
        .data_if  = data_if,
    };
    s_codec_handle = esp_codec_dev_new(&dev_cfg);
    if (!s_codec_handle) {
        ESP_LOGE(TAG, "Failed to create codec device");
        return ESP_FAIL;
    }

    /* Open the codec with sample configuration */
    esp_codec_dev_sample_info_t sample_cfg = {
        .bits_per_sample = I2S_DATA_BIT_WIDTH_16BIT,
        .channel = 2,
        .channel_mask = 0x03,
        .sample_rate = (uint32_t)cfg->sample_rate,
    };
    if (esp_codec_dev_open(s_codec_handle, &sample_cfg) != ESP_CODEC_DEV_OK) {
        ESP_LOGE(TAG, "Failed to open codec device");
        return ESP_FAIL;
    }

    /* Set initial volume */
    if (esp_codec_dev_set_out_vol(s_codec_handle, cfg->volume) != ESP_CODEC_DEV_OK) {
        ESP_LOGW(TAG, "Failed to set initial volume");
    }

    ESP_LOGI(TAG, "ES8311 codec initialized (I2C addr=0x%02x, vol=%d)",
             ES8311_CODEC_DEFAULT_ADDR, cfg->volume);
    return ESP_OK;
}

/* ═══ Public API ══════════════════════════════════════════════════ */

esp_err_t audio_init(const audio_config_t *config)
{
    if (s_initialized) {
        ESP_LOGW(TAG, "Audio already initialized");
        return ESP_OK;
    }
    if (!config || !config->i2c_handle) {
        ESP_LOGE(TAG, "Invalid config or missing I2C handle");
        return ESP_ERR_INVALID_ARG;
    }

    s_config = *config;

    /* 1. Initialize I2S driver */
    ESP_RETURN_ON_ERROR(i2s_driver_init(config), TAG, "I2S init failed");

    /* 2. Initialize ES8311 codec via I2C */
    ESP_RETURN_ON_ERROR(es8311_codec_init(config), TAG, "ES8311 codec init failed");

    s_initialized = true;
    ESP_LOGI(TAG, "Audio subsystem ready");
    return ESP_OK;
}

esp_err_t audio_play_tone(uint32_t freq_hz, uint32_t duration_ms, int volume)
{
    if (!s_initialized) return ESP_ERR_INVALID_STATE;

    /* Set volume */
    esp_codec_dev_set_out_vol(s_codec_handle, volume);

    /*
     * Generate a 16-bit stereo sine wave.
     * Buffer size: 1024 stereo samples (4096 bytes) per chunk.
     */
    const int sr = s_config.sample_rate;
    const int chunk_samples = 1024;  /* stereo sample pairs per chunk */
    const size_t chunk_bytes = chunk_samples * 2 * sizeof(int16_t); /* L+R 16-bit */
    int16_t *buf = (int16_t *)heap_caps_malloc(chunk_bytes, MALLOC_CAP_INTERNAL);
    if (!buf) {
        ESP_LOGE(TAG, "Failed to allocate tone buffer");
        return ESP_ERR_NO_MEM;
    }

    const double phase_inc = 2.0 * M_PI * freq_hz / sr;
    double phase = 0.0;
    const int16_t amplitude = 16000;  /* ~50% of full scale */

    uint32_t total_samples = (duration_ms > 0) ? (sr * duration_ms / 1000) : 0;
    uint32_t samples_written = 0;
    size_t bytes_written = 0;

    ESP_LOGI(TAG, "Playing %u Hz tone for %u ms (vol=%d, sr=%d)",
             (unsigned)freq_hz, (unsigned)duration_ms, volume, sr);

    while (duration_ms == 0 || samples_written < total_samples) {
        int samples_this_chunk = chunk_samples;
        if (duration_ms > 0 && (total_samples - samples_written) < (uint32_t)chunk_samples) {
            samples_this_chunk = (int)(total_samples - samples_written);
        }

        for (int i = 0; i < samples_this_chunk; i++) {
            int16_t val = (int16_t)(amplitude * sin(phase));
            buf[i * 2]     = val;  /* Left */
            buf[i * 2 + 1] = val;  /* Right */
            phase += phase_inc;
            if (phase >= 2.0 * M_PI) phase -= 2.0 * M_PI;
        }

        esp_err_t ret = i2s_channel_write(s_tx_handle, buf,
                                           samples_this_chunk * 2 * sizeof(int16_t),
                                           &bytes_written, portMAX_DELAY);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "I2S write failed: %s", esp_err_to_name(ret));
            heap_caps_free(buf);
            return ret;
        }
        samples_written += samples_this_chunk;
    }

    heap_caps_free(buf);
    ESP_LOGI(TAG, "Tone playback complete (%u samples)", (unsigned)samples_written);
    return ESP_OK;
}

esp_err_t audio_stop(void)
{
    if (!s_initialized) return ESP_ERR_INVALID_STATE;

    /* Disable and re-enable TX to flush */
    i2s_channel_disable(s_tx_handle);
    i2s_channel_enable(s_tx_handle);
    return ESP_OK;
}

esp_err_t audio_set_volume(int volume)
{
    if (!s_initialized) return ESP_ERR_INVALID_STATE;
    if (volume < 0) volume = 0;
    if (volume > 100) volume = 100;

    if (esp_codec_dev_set_out_vol(s_codec_handle, volume) != ESP_CODEC_DEV_OK) {
        ESP_LOGE(TAG, "Failed to set volume to %d", volume);
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "Volume set to %d", volume);
    return ESP_OK;
}

esp_err_t audio_set_sample_rate(int sample_rate)
{
    if (!s_initialized || !s_tx_handle) return ESP_ERR_INVALID_STATE;
    if (sample_rate == s_config.sample_rate) return ESP_OK;  /* no change */

    /* If the cached rate was reset to 0 (by audio_reset_sample_rate after
     * a PSRAM app exit), just update the cached value without touching I2S.
     * The I2S channel is still running at its configured hardware rate;
     * the next app's audio_submit calls will work regardless. */
    if (s_config.sample_rate == 0) {
        s_config.sample_rate = sample_rate;
        ESP_LOGI(TAG, "I2S sample rate cached as %d Hz (no reconfig)", sample_rate);
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Reconfiguring I2S sample rate: %d -> %d", s_config.sample_rate, sample_rate);

    /* Disable TX channel, reconfigure clocks, re-enable */
    i2s_channel_disable(s_tx_handle);

    i2s_std_clk_config_t clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(sample_rate);
    clk_cfg.mclk_multiple = (i2s_mclk_multiple_t)MCLK_MULTIPLE;
    esp_err_t ret = i2s_channel_reconfig_std_clock(s_tx_handle, &clk_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to reconfigure I2S clock: %s", esp_err_to_name(ret));
        i2s_channel_enable(s_tx_handle);  /* try to restore */
        return ret;
    }

    i2s_channel_enable(s_tx_handle);
    s_config.sample_rate = sample_rate;
    ESP_LOGI(TAG, "I2S sample rate set to %d Hz", sample_rate);
    return ESP_OK;
}

void audio_reset_sample_rate(void)
{
    /* Reset cached sample rate to 0 so next audio_set_sample_rate()
     * forces a full reconfiguration via disable/reconfig/enable.
     * Call this AFTER draining DMA (e.g. audio_submit_zero) so the
     * next i2s_channel_disable won't hang on pending transfers. */
    s_config.sample_rate = 0;
}

esp_err_t audio_play_pcm(const void *data, size_t len, int sample_rate)
{
    if (!s_initialized) return ESP_ERR_INVALID_STATE;
    if (!data || len == 0) return ESP_ERR_INVALID_ARG;

    size_t bytes_written = 0;
    const uint8_t *ptr = (const uint8_t *)data;
    size_t remaining = len;

    while (remaining > 0) {
        size_t to_write = (remaining > 4096) ? 4096 : remaining;
        esp_err_t ret = i2s_channel_write(s_tx_handle, ptr, to_write,
                                           &bytes_written, portMAX_DELAY);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "PCM write failed: %s", esp_err_to_name(ret));
            return ret;
        }
        ptr += bytes_written;
        remaining -= bytes_written;
    }

    return ESP_OK;
}
