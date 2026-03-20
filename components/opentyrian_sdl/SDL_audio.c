/*
 * SDL Audio shim for ESP32-P4
 *
 * Replaces built-in DAC I2S with ES8311 codec via audio_play_pcm().
 * The game's audio callback fills a buffer with signed 16-bit PCM,
 * which we pass directly to the P4 audio driver.
 */
#include "SDL_audio.h"
#include "audio.h"
#include <string.h>
#include <stdlib.h>

static SDL_AudioSpec audio_spec;
static uint8_t *sdl_buffer = NULL;
static volatile bool audio_paused = true;
static volatile bool audio_locked = false;
static TaskHandle_t audio_task_handle = NULL;

static void audio_update_task(void *arg)
{
    /* Stereo interleaved buffer: SAMPLECOUNT frames × 2 channels × 2 bytes */
    const int mono_bytes = SAMPLECOUNT * SAMPLESIZE;
    const int stereo_bytes = mono_bytes * 2;
    int16_t *stereo_buf = heap_caps_malloc(stereo_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (stereo_buf == NULL) {
        printf("SDL audio: stereo buffer alloc failed\n");
        vTaskDelete(NULL);
        return;
    }

    while (1) {
        if (!audio_paused && !audio_locked && sdl_buffer != NULL) {
            /* Clear and fill mono buffer via game callback */
            memset(sdl_buffer, 0, mono_bytes);
            if (audio_spec.callback) {
                audio_spec.callback(audio_spec.userdata, sdl_buffer, mono_bytes);
            }

            /* Convert mono signed-16 to stereo interleaved for ES8311 */
            const int16_t *mono = (const int16_t *)sdl_buffer;
            for (int i = 0; i < SAMPLECOUNT; i++) {
                stereo_buf[i * 2]     = mono[i];
                stereo_buf[i * 2 + 1] = mono[i];
            }

            audio_play_pcm(stereo_buf, stereo_bytes, SAMPLERATE);
        } else {
            vTaskDelay(pdMS_TO_TICKS(5));
        }
    }
}

void SDL_AudioInit(void)
{
    sdl_buffer = heap_caps_malloc(SAMPLECOUNT * SAMPLESIZE * 2, MALLOC_CAP_8BIT);
    if (sdl_buffer == NULL) {
        printf("SDL_AudioInit: audio buffer alloc failed!\n");
    }
}

int SDL_OpenAudio(SDL_AudioSpec *desired, SDL_AudioSpec *obtained)
{
    SDL_AudioInit();

    memset(obtained, 0, sizeof(SDL_AudioSpec));
    obtained->freq = SAMPLERATE;
    obtained->format = 16;
    obtained->channels = 1;
    obtained->samples = SAMPLECOUNT;
    obtained->callback = desired->callback;
    obtained->userdata = desired->userdata;
    memcpy(&audio_spec, obtained, sizeof(SDL_AudioSpec));

    /* Set the audio sample rate to match game audio */
    audio_set_sample_rate(SAMPLERATE);

    xTaskCreatePinnedToCore(audio_update_task, "sdl_audio", 8192, NULL, 3, &audio_task_handle, 1);
    printf("SDL audio task started (rate=%d)\n", SAMPLERATE);
    return 0;
}

void SDL_PauseAudio(int pause_on)
{
    audio_paused = pause_on ? true : false;
}

void SDL_CloseAudio(void)
{
    audio_paused = true;
    if (audio_task_handle) {
        vTaskDelete(audio_task_handle);
        audio_task_handle = NULL;
    }
    if (sdl_buffer != NULL) {
        free(sdl_buffer);
        sdl_buffer = NULL;
    }
}

int SDL_BuildAudioCVT(SDL_AudioCVT *cvt, Uint16 src_format, Uint8 src_channels,
                       int src_rate, Uint16 dst_format, Uint8 dst_channels, int dst_rate)
{
    cvt->len_mult = 1;
    return 0;
}

IRAM_ATTR int SDL_ConvertAudio(SDL_AudioCVT *cvt)
{
    /*
     * On P4 with ES8311 codec, we output proper signed 16-bit PCM.
     * The original DAC differential conversion is not needed.
     * The game expects mono→stereo duplication which we do in the task.
     * Here we just expand mono samples in-place: each Sint16 sample
     * is duplicated to fill L+R channels.
     */
    Sint16 *sbuf = (Sint16 *)cvt->buf;
    int num_samples = cvt->len / 2;  /* number of Sint16 samples */

    /* Expand mono to stereo in-place (work backwards to avoid overwrite) */
    int16_t *dst = (int16_t *)cvt->buf;
    for (int i = num_samples - 1; i >= 0; i--) {
        dst[i * 2]     = sbuf[i];
        dst[i * 2 + 1] = sbuf[i];
    }
    cvt->len_cvt = cvt->len * 2;

    return 0;
}

void SDL_LockAudio(void)
{
    audio_locked = true;
}

void SDL_UnlockAudio(void)
{
    audio_locked = false;
}
