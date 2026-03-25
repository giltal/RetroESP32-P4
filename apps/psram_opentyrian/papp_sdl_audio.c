/*
 * SDL Audio shim for PSRAM App — routes through app_services_t.
 * Replaces components/opentyrian_sdl/SDL_audio.c
 */
#include "psram_app.h"
#include "SDL_audio.h"
#include <string.h>
#include <stdlib.h>

extern const app_services_t *_papp_svc;

extern volatile int papp_exit_requested;

static SDL_AudioSpec audio_spec;
static uint8_t *sdl_buffer = NULL;
static volatile int audio_paused = 1;
static volatile int audio_locked = 0;
static volatile int audio_task_done = 0;
static void *audio_task_handle = NULL;
static int16_t *stereo_buf = NULL;   /* file-scope so SDL_CloseAudio can free */

static void audio_update_task(void *arg)
{
    const int mono_bytes = SAMPLECOUNT * SAMPLESIZE;
    stereo_buf = _papp_svc->mem_caps_alloc(
        SAMPLECOUNT * 2 * sizeof(int16_t), PAPP_MEM_CAP_INTERNAL | PAPP_MEM_CAP_DMA);
    if (stereo_buf == NULL) {
        _papp_svc->log_printf("PAPP audio: stereo buffer alloc failed\n");
        audio_task_done = 1;
        while (1) _papp_svc->delay_ms(100);
    }

    while (!papp_exit_requested) {
        if (!audio_paused && !audio_locked && sdl_buffer != NULL) {
            memset(sdl_buffer, 0, mono_bytes);
            if (audio_spec.callback) {
                audio_spec.callback(audio_spec.userdata, sdl_buffer, mono_bytes);
            }

            /* Mono signed-16 → stereo interleaved */
            const int16_t *mono = (const int16_t *)sdl_buffer;
            for (int i = 0; i < SAMPLECOUNT; i++) {
                stereo_buf[i * 2]     = mono[i];
                stereo_buf[i * 2 + 1] = mono[i];
            }

            /* Submit as stereo frames via services */
            _papp_svc->audio_submit((short *)stereo_buf, SAMPLECOUNT);
        } else {
            _papp_svc->delay_ms(5);
        }
    }
    audio_task_done = 1;
    /* Do NOT return — FreeRTOS tasks must never return (causes abort).
       Spin here until cleanup deletes this task. */
    while (1) _papp_svc->delay_ms(100);
}

void SDL_AudioInit(void)
{
    sdl_buffer = _papp_svc->mem_caps_alloc(SAMPLECOUNT * SAMPLESIZE * 2,
                                           PAPP_MEM_CAP_INTERNAL);
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

    _papp_svc->audio_init(SAMPLERATE);

    /* Audio task on core 1 */
    _papp_svc->task_create(audio_update_task, "ot_audio", 8192, NULL, 8,
                           &audio_task_handle, 1);
    return 0;
}

void SDL_PauseAudio(int pause_on)
{
    audio_paused = pause_on ? 1 : 0;
}

void SDL_CloseAudio(void)
{
    audio_paused = 1;
    /* Let the audio task exit its loop gracefully so it releases
     * the I2S driver's internal mutex (vTaskDelete while inside
     * i2s_channel_write deadlocks the mutex permanently). */
    if (audio_task_handle) {
        /* papp_exit_requested is already set by the exit path */
        for (int i = 0; i < 100 && !audio_task_done; i++)
            _papp_svc->delay_ms(10);
        /* Always delete — task is either spinning (done) or stuck */
        _papp_svc->task_delete(audio_task_handle);
        audio_task_handle = NULL;
    }
    if (stereo_buf != NULL) {
        _papp_svc->mem_free(stereo_buf);
        stereo_buf = NULL;
    }
    if (sdl_buffer != NULL) {
        _papp_svc->mem_free(sdl_buffer);
        sdl_buffer = NULL;
    }
}

int SDL_BuildAudioCVT(SDL_AudioCVT *cvt, Uint16 src_format, Uint8 src_channels,
                       int src_rate, Uint16 dst_format, Uint8 dst_channels, int dst_rate)
{
    cvt->len_mult = 1;
    return 0;
}

int SDL_ConvertAudio(SDL_AudioCVT *cvt)
{
    int16_t *sbuf = (int16_t *)cvt->buf;
    int num_samples = cvt->len / 2;

    int16_t *dst = (int16_t *)cvt->buf;
    for (int i = num_samples - 1; i >= 0; i--) {
        dst[i * 2]     = sbuf[i];
        dst[i * 2 + 1] = sbuf[i];
    }
    cvt->len_cvt = cvt->len * 2;
    return 0;
}

void SDL_LockAudio(void) { audio_locked = 1; }
void SDL_UnlockAudio(void) { audio_locked = 0; }
