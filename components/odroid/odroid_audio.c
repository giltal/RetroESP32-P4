/*
 * Odroid Audio Compatibility Layer — ESP32-P4 Implementation
 *
 * Wraps the ES8311 codec audio component.
 * Audio hardware is initialized in odroid_system_init(), so this
 * just provides the volume control and PCM submission interfaces.
 */

#include "odroid_audio.h"
#include "odroid_settings.h"
#include "audio.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "odroid_audio";
static int s_sample_rate = 16000;
static odroid_volume_level s_volume_level = ODROID_VOLUME_LEVEL3;

/* Volume table: software attenuation factors × 1000 */
static const int volume_table[ODROID_VOLUME_LEVEL_COUNT] = {0, 125, 250, 500, 1000};

void odroid_audio_init(int sample_rate)
{
    s_sample_rate = sample_rate;

    /* Reconfigure I2S hardware to the requested sample rate */
    esp_err_t ret = audio_set_sample_rate(sample_rate);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to set sample rate to %d: %s", sample_rate, esp_err_to_name(ret));
    }

    /* Restore persisted volume level. */
    odroid_volume_level level = (odroid_volume_level)odroid_settings_Volume_get();
    if (level >= ODROID_VOLUME_LEVEL_COUNT) level = ODROID_VOLUME_LEVEL3;
    s_volume_level = level;
    ESP_LOGI(TAG, "Audio init (compat): sample_rate=%d, vol=%d", sample_rate, level);
}

void odroid_audio_terminate(void)
{
    audio_stop();
    ESP_LOGI(TAG, "Audio terminated");
}

void odroid_audio_volume_set(int volume)
{
    if (volume < 0) volume = 0;
    if (volume >= ODROID_VOLUME_LEVEL_COUNT) volume = ODROID_VOLUME_LEVEL_COUNT - 1;
    s_volume_level = (odroid_volume_level)volume;

    /* Map to 0-100% for the ES8311 codec */
    int pct = (volume * 100) / (ODROID_VOLUME_LEVEL_COUNT - 1);
    audio_set_volume(pct);
    ESP_LOGI(TAG, "Volume set: %d/%d → %d%%", volume, ODROID_VOLUME_LEVEL_COUNT - 1, pct);
}

odroid_volume_level odroid_audio_volume_get(void)
{
    return s_volume_level;
}

void odroid_audio_volume_change(void)
{
    int level = (s_volume_level + 1) % ODROID_VOLUME_LEVEL_COUNT;
    odroid_audio_volume_set(level);
    odroid_settings_Volume_set(level);
}

void odroid_audio_submit(short *stereoAudioBuffer, int frameCount)
{
    if (!stereoAudioBuffer || frameCount <= 0) return;

    int total_samples = frameCount * 2; /* stereo: L, R, L, R, ... */

    /* Apply software volume attenuation */
    float vol = (float)volume_table[s_volume_level] * 0.001f;
    if (vol < 1.0f) {
        for (int i = 0; i < total_samples; ++i) {
            stereoAudioBuffer[i] = (short)((float)stereoAudioBuffer[i] * vol);
        }
    }

    size_t len = total_samples * sizeof(short);
    audio_play_pcm(stereoAudioBuffer, len, s_sample_rate);
}

void odroid_audio_submit_zero(void)
{
    /* Submit a small silent buffer to clear any DMA queues */
    short silence[512];
    memset(silence, 0, sizeof(silence));
    audio_play_pcm(silence, sizeof(silence), s_sample_rate);
}

int odroid_audio_sample_rate_get(void)
{
    return s_sample_rate;
}
