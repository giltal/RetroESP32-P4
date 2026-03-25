/*
 * Doom I_Sound shim for PSRAM App — replaces retro-go audio from main.c.
 *
 * 8-channel SFX mixer + OPL music synthesizer running in a dedicated task.
 * Audio is submitted to the launcher via app_services_t::audio_submit().
 */
#include "psram_app.h"
#include <string.h>
#include <stdint.h>

/* prboom engine headers */
#include <doomdef.h>
#include <doomstat.h>
#include <sounds.h>
#include <s_sound.h>
#include <i_sound.h>
#include <w_wad.h>
#include <oplplayer.h>
#include <mus2mid.h>
#include <midifile.h>

extern const app_services_t *_papp_svc;
extern volatile int papp_exit_requested;

#define AUDIO_SAMPLE_RATE   22050
#define AUDIO_BUFFER_LENGTH (AUDIO_SAMPLE_RATE / TICRATE + 1)
#define NUM_MIX_CHANNELS    8

/* Expected variables by doom engine */
int snd_card = 1, mus_card = 1;
int snd_samplerate = AUDIO_SAMPLE_RATE;

typedef struct {
    uint16_t unused1;
    uint16_t samplerate;
    uint16_t length;
    uint16_t unused2;
    uint8_t samples[];
} doom_sfx_t;

typedef struct {
    const doom_sfx_t *sfx;
    size_t pos;
    float factor;
    int starttic;
} channel_t;

static channel_t channels[NUM_MIX_CHANNELS];
static const doom_sfx_t *sfx[NUMSFX];

/* Mix buffer: stereo interleaved int16 (same layout as rg_audio_sample_t) */
static int16_t mixbuffer[AUDIO_BUFFER_LENGTH * 2];

static const music_player_t *music_player = &opl_synth_player;
static bool musicPlaying = false;

/* Sound task handle — accessible for cleanup */
void *doom_sound_task_handle = NULL;
static volatile int doom_sound_task_done = 0;

extern int gametic;

/* ── SFX Interface ───────────────────────────────────────────────────── */

void I_UpdateSoundParams(int handle, int volume, int seperation, int pitch)
{
}

int I_StartSound(int sfxid, int channel, int vol, int sep, int pitch, int priority)
{
    int oldest = gametic;
    int slot = 0;

    if (!sfx[sfxid])
        return -1;

    /* Some sounds play only once at a time */
    if (sfxid == sfx_sawup || sfxid == sfx_sawidl || sfxid == sfx_sawful
        || sfxid == sfx_sawhit || sfxid == sfx_stnmov || sfxid == sfx_pistol) {
        for (int i = 0; i < NUM_MIX_CHANNELS; i++) {
            if (channels[i].sfx == sfx[sfxid])
                channels[i].sfx = NULL;
        }
    }

    /* Find available channel or steal the oldest */
    for (int i = 0; i < NUM_MIX_CHANNELS; i++) {
        if (channels[i].sfx == NULL) {
            slot = i;
            break;
        } else if (channels[i].starttic < oldest) {
            slot = i;
            oldest = channels[i].starttic;
        }
    }

    channel_t *chan = &channels[slot];
    chan->sfx = sfx[sfxid];
    chan->factor = (float)chan->sfx->samplerate / snd_samplerate;
    chan->pos = 0;
    chan->starttic = gametic;

    return slot;
}

void I_StopSound(int handle)
{
    if (handle < NUM_MIX_CHANNELS)
        channels[handle].sfx = NULL;
}

bool I_SoundIsPlaying(int handle)
{
    return false;
}

bool I_AnySoundStillPlaying(void)
{
    for (int i = 0; i < NUM_MIX_CHANNELS; i++)
        if (channels[i].sfx)
            return true;
    return false;
}

/* ── Sound Task (runs on core 1) ─────────────────────────────────────── */

static void soundTask(void *arg)
{
    while (!papp_exit_requested) {
        bool haveMusic = snd_MusicVolume > 0 && musicPlaying;
        bool haveSFX = snd_SfxVolume > 0 && I_AnySoundStillPlaying();

        if (haveMusic) {
            music_player->render(mixbuffer, AUDIO_BUFFER_LENGTH);
        }

        if (haveSFX) {
            int16_t *audioBuffer = mixbuffer;
            int16_t *audioBufferEnd = audioBuffer + AUDIO_BUFFER_LENGTH * 2;
            while (audioBuffer < audioBufferEnd) {
                int totalSample = 0;
                int totalSources = 0;
                int sample;

                for (int i = 0; i < NUM_MIX_CHANNELS; i++) {
                    channel_t *chan = &channels[i];
                    if (!chan->sfx)
                        continue;
                    size_t pos = (size_t)(chan->pos++ * chan->factor);
                    if (pos >= chan->sfx->length) {
                        chan->sfx = NULL;
                    } else if ((sample = chan->sfx->samples[pos])) {
                        totalSample += sample - 127;
                        totalSources++;
                    }
                }

                totalSample <<= 7;
                totalSample /= (16 - snd_SfxVolume);

                if (haveMusic) {
                    totalSample += *audioBuffer;
                    totalSources += (totalSources == 0);
                }

                if (totalSources > 0)
                    totalSample /= totalSources;

                if (totalSample > 32767)
                    totalSample = 32767;
                else if (totalSample < -32768)
                    totalSample = -32768;

                *audioBuffer++ = totalSample;
                *audioBuffer++ = totalSample;
            }
        }

        if (!haveMusic && !haveSFX) {
            __builtin_memset(mixbuffer, 0, sizeof(mixbuffer));
        }

        /* Submit stereo frames to launcher audio */
        _papp_svc->audio_submit((short *)mixbuffer, AUDIO_BUFFER_LENGTH);
    }
    doom_sound_task_done = 1;
    /* Do NOT return — FreeRTOS tasks must never return (causes abort).
       Spin here until cleanup deletes this task. */
    while (1) _papp_svc->delay_ms(100);
}

/* ── Init / Shutdown ─────────────────────────────────────────────────── */

void I_InitSound(void)
{
    for (int i = 1; i < NUMSFX; i++) {
        if (S_sfx[i].lumpnum != -1)
            sfx[i] = W_CacheLumpNum(S_sfx[i].lumpnum);
    }

    music_player->init(snd_samplerate);
    music_player->setvolume(snd_MusicVolume);

    _papp_svc->audio_init(AUDIO_SAMPLE_RATE);
    _papp_svc->task_create(soundTask, "doom_snd", 4096, NULL, 8,
                           &doom_sound_task_handle, 1);
}

void I_ShutdownSound(void)
{
    music_player->shutdown();
}

void papp_sound_shutdown(void)
{
    /* Wait for the sound task to exit its loop gracefully.
     * Force-killing with vTaskDelete while inside i2s_channel_write
     * deadlocks the I2S driver's internal mutex. */
    if (doom_sound_task_handle) {
        /* papp_exit_requested is already set by the exit path */
        for (int i = 0; i < 100 && !doom_sound_task_done; i++)
            _papp_svc->delay_ms(10);
        /* Always delete — task is either spinning (done) or stuck */
        _papp_svc->task_delete(doom_sound_task_handle);
        doom_sound_task_handle = NULL;
    }
}

/* ── Music Interface ─────────────────────────────────────────────────── */

void I_PlaySong(int handle, int looping)
{
    music_player->play((void *)(intptr_t)handle, looping);
    musicPlaying = true;
}

void I_PauseSong(int handle)
{
    music_player->pause();
    musicPlaying = false;
}

void I_ResumeSong(int handle)
{
    music_player->resume();
    musicPlaying = true;
}

void I_StopSong(int handle)
{
    music_player->stop();
    musicPlaying = false;
}

void I_UnRegisterSong(int handle)
{
    music_player->unregistersong((void *)(intptr_t)handle);
}

int I_RegisterSong(const void *data, size_t len)
{
    uint8_t *mid = NULL;
    size_t midlen;
    int handle = 0;

    if (mus2mid(data, len, &mid, &midlen, 64) == 0)
        handle = (int)(intptr_t)music_player->registersong(mid, midlen);
    else
        handle = (int)(intptr_t)music_player->registersong(data, len);

    free(mid);
    return handle;
}

void I_SetMusicVolume(int volume)
{
    music_player->setvolume(volume);
}

/* Stubs for unused sound functions */
void I_SetChannels(void) {}
