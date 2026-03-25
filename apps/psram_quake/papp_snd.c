/*
 * Quake Sound shim for PSRAM App — replaces snd_esp32.c.
 *
 * Keeps the same mixer architecture but routes audio output through
 * app_services_t::audio_submit() instead of rg_audio_submit().
 * The audio_task runs on a separate core via service table task_create.
 */
#include "psram_app.h"
#include <string.h>
#include <stdint.h>

#include "quakedef.h"

extern const app_services_t *_papp_svc;
extern volatile int papp_exit_requested;

/* Forward declarations from common.c */
byte *COM_LoadFile(char *path, int usehunk);

#define MAX_SFX 256

/* Generation counter to invalidate channels across level changes */
static int sound_generation = 0;

static sfx_t known_sfx[MAX_SFX];
channel_t channels[MAX_CHANNELS];

static int num_sfx = 0;
static sfx_t *ambient_sfx[NUM_AMBIENTS];
static bool snd_ambient = 1;

/* No real mutex — single-threaded access from audio task + game */
static volatile int sound_lock = 0;

/* sound.h visible stuff */
cvar_t bgmvolume = {"bgmvolume", "1", true};
cvar_t volume = {"volume", "1.0", true};
cvar_t nosound = {"nosound", "0"};
cvar_t ambient_level = {"ambient_level", "0.3"};
cvar_t ambient_fade = {"ambient_fade", "100"};

int total_channels;

qboolean snd_initialized = false;

vec3_t listener_origin;
vec3_t listener_forward;
vec3_t listener_right;
vec3_t listener_up;
vec_t sound_nominal_clip_dist = 1000.0;

int paintedtime;
static int64_t hardware_sample_offset = 0;
static int64_t total_samples_submitted = 0;

#define AUDIO_BUFFER_SAMPLES 512

static int64_t mix_buffer[AUDIO_BUFFER_SAMPLES * 2];
static int16_t output_buffer[AUDIO_BUFFER_SAMPLES * 2]; /* stereo interleaved L,R */

/* Sound task handle — accessible for cleanup */
void *quake_sound_task_handle = NULL;
static volatile int quake_sound_task_done = 0;

/* Forward declarations */
static sfx_t *FindSfxName(char *name);

/* LoadSound: loads a WAV file into Hunk memory */
static bool LoadSound(sfx_t *s)
{
    char namebuffer[256];
    sprintf(namebuffer, "sound/%s", s->name);

    byte *data = COM_LoadFile(namebuffer, 1);

    if (!data)
        return false;

    wavinfo_t info = GetWavinfo(s->name, (byte *)data, com_filesize);

    if (info.channels != 1) {
        return false;
    }

    s->cache.data = data + info.dataofs;
    s->cache.sampleRate = info.rate;
    s->cache.sampleWidth = info.width;
    s->cache.loopStart = info.loopstart;
    s->cache.sampleCount = info.samples;

    int outputRate = 22050;
    s->cache.stepFixedPoint = (uint32_t)((float)info.rate * ESP32_SOUND_STEP / outputRate);
    s->cache.effectiveLength = (s->cache.sampleCount * (int64_t)ESP32_SOUND_STEP) / s->cache.stepFixedPoint;

    return true;
}

static sfx_t *FindSfxName(char *name)
{
    if (name == NULL) return NULL;
    if (strlen(name) >= MAX_QPATH) return NULL;

    for (int i = 0; i < num_sfx; ++i)
        if (!strcmp(known_sfx[i].name, name))
            return &known_sfx[i];

    if (num_sfx == MAX_SFX) return NULL;

    sfx_t *sfx = &known_sfx[num_sfx];
    strcpy(sfx->name, name);

    if (!LoadSound(sfx)) return NULL;

    num_sfx++;
    return sfx;
}

static void UpdateAmbientSounds(void)
{
    mleaf_t *l;
    float vol;

    if (!snd_initialized || !snd_ambient || !cl.worldmodel) return;

    l = Mod_PointInLeaf(listener_origin, cl.worldmodel);
    if (!l || !ambient_level.value) {
        for (int i = 0; i < NUM_AMBIENTS; i++)
            channels[i].sfx = NULL;
        return;
    }

    for (int i = 0; i < NUM_AMBIENTS; i++) {
        channels[i].sfx = ambient_sfx[i];
        vol = ambient_level.value * l->ambient_sound_level[i];
        if (vol < 8) vol = 0;

        if (channels[i].master_vol < vol) {
            channels[i].master_vol += host_frametime * ambient_fade.value;
            if (channels[i].master_vol > vol) channels[i].master_vol = vol;
        } else if (channels[i].master_vol > vol) {
            channels[i].master_vol -= host_frametime * ambient_fade.value;
            if (channels[i].master_vol < vol) channels[i].master_vol = vol;
        }

        channels[i].leftvol = channels[i].rightvol = (int)channels[i].master_vol;
    }
}

channel_t *SND_PickChannel(int entnum, int entchannel)
{
    int first_to_die = -1;
    int life_left = 0x7fffffff;

    for (int i = NUM_AMBIENTS; i < NUM_AMBIENTS + MAX_DYNAMIC_CHANNELS; ++i) {
        if (entchannel != 0 && channels[i].entnum == entnum &&
            (channels[i].entchannel == entchannel || entchannel == -1)) {
            first_to_die = i;
            break;
        }
        if (channels[i].entnum == cl.viewentity && entnum != cl.viewentity && channels[i].sfx)
            continue;
        if (channels[i].end - paintedtime < life_left) {
            life_left = channels[i].end - paintedtime;
            first_to_die = i;
        }
    }

    if (first_to_die == -1) return NULL;
    channels[first_to_die].sfx = NULL;
    channels[first_to_die].generation = sound_generation;
    return &channels[first_to_die];
}

void SND_Spatialize(channel_t *ch)
{
    vec_t dot, dist, lscale, rscale, scale;
    vec3_t source_vec;

    if (ch->entnum == cl.viewentity) {
        ch->leftvol = ch->master_vol;
        ch->rightvol = ch->master_vol;
        return;
    }

    VectorSubtract(ch->origin, listener_origin, source_vec);
    dist = VectorNormalize(source_vec) * ch->dist_mult;
    dot = DotProduct(listener_right, source_vec);

    rscale = 1.0 + dot;
    lscale = 1.0 - dot;

    scale = (1.0 - dist) * rscale;
    ch->rightvol = (int)(ch->master_vol * scale);
    if (ch->rightvol < 0) ch->rightvol = 0;

    scale = (1.0 - dist) * lscale;
    ch->leftvol = (int)(ch->master_vol * scale);
    if (ch->leftvol < 0) ch->leftvol = 0;
}

static void audio_task(void *arg)
{
    while (!papp_exit_requested && snd_initialized) {
        _papp_svc->delay_ms(1);

        memset(mix_buffer, 0, sizeof(mix_buffer));
        int volumeInt = (int)(volume.value * 256);
        int current_gen = sound_generation;

        for (int i = 0; i < total_channels; ++i) {
            channel_t *chan = &channels[i];

            if (chan->generation != current_gen) {
                chan->sfx = NULL;
                continue;
            }

            if (!chan->sfx || (!chan->leftvol && !chan->rightvol)) continue;

            sfxcache_t *cache = &chan->sfx->cache;
            if (!cache->data) continue;

            int pos = chan->pos;
            int length = cache->sampleCount * ESP32_SOUND_STEP;
            uint32_t step = cache->stepFixedPoint;
            int width = cache->sampleWidth;
            uint8_t *data = (uint8_t *)cache->data;
            int lvol = chan->leftvol;
            int rvol = chan->rightvol;

            for (int j = 0; j < AUDIO_BUFFER_SAMPLES; ++j) {
                if (pos >= length) {
                    if (cache->loopStart < 0) {
                        chan->sfx = NULL;
                        break;
                    }
                    pos = (cache->loopStart * ESP32_SOUND_STEP) + (pos % length);
                }

                int32_t sample;
                uint8_t *p = data + (pos >> 12) * width;
                if (width == 1)
                    sample = ((int32_t)*p - 128) << 8;
                else
                    sample = (int16_t)(p[0] | (p[1] << 8));

                mix_buffer[2 * j] += (int64_t)sample * lvol;
                mix_buffer[2 * j + 1] += (int64_t)sample * rvol;
                pos += step;
            }
            chan->pos = pos;
        }

        for (int i = 0; i < AUDIO_BUFFER_SAMPLES; ++i) {
            int32_t l = (int32_t)((mix_buffer[2 * i] * volumeInt) >> 16);
            int32_t r = (int32_t)((mix_buffer[2 * i + 1] * volumeInt) >> 16);

            if (l > 32767) l = 32767; else if (l < -32768) l = -32768;
            if (r > 32767) r = 32767; else if (r < -32768) r = -32768;
            output_buffer[i * 2] = (int16_t)l;
            output_buffer[i * 2 + 1] = (int16_t)r;
        }

        paintedtime += AUDIO_BUFFER_SAMPLES;
        total_samples_submitted += AUDIO_BUFFER_SAMPLES;

        _papp_svc->audio_submit((short *)output_buffer, AUDIO_BUFFER_SAMPLES);
    }
    quake_sound_task_done = 1;
    while (1) _papp_svc->delay_ms(100);
}

void S_Init(void)
{
    Cvar_RegisterVariable(&bgmvolume);
    Cvar_RegisterVariable(&volume);
    Cvar_RegisterVariable(&nosound);
    Cvar_RegisterVariable(&ambient_level);
    Cvar_RegisterVariable(&ambient_fade);

    total_channels = MAX_DYNAMIC_CHANNELS + NUM_AMBIENTS;
    memset(channels, 0, sizeof(channels));
    sound_generation = 1;

    snd_initialized = true;
    paintedtime = 0;
    total_samples_submitted = 0;

    _papp_svc->audio_init(22050);
    _papp_svc->task_create(audio_task, "quake_snd", 4096, NULL, 8,
                           &quake_sound_task_handle, 1);
}

void S_AmbientOff(void) { snd_ambient = false; }
void S_AmbientOn(void) { snd_ambient = true; }

void S_Shutdown(void)
{
    if (!snd_initialized) return;
    snd_initialized = false;
    _papp_svc->delay_ms(100);
}

void papp_sound_shutdown(void)
{
    if (quake_sound_task_handle) {
        for (int i = 0; i < 100 && !quake_sound_task_done; i++)
            _papp_svc->delay_ms(10);
        _papp_svc->task_delete(quake_sound_task_handle);
        quake_sound_task_handle = NULL;
    }
}

void S_StartSound(int entnum, int entchannel, sfx_t *sfx, vec3_t origin,
                  float fvol, float attenuation)
{
    if (!snd_initialized || nosound.value || !sfx) return;

    channel_t *target_chan = SND_PickChannel(entnum, entchannel);
    if (!target_chan) return;

    memset(target_chan, 0, sizeof(*target_chan));
    VectorCopy(origin, target_chan->origin);
    target_chan->dist_mult = attenuation / sound_nominal_clip_dist;
    target_chan->master_vol = (int)(fvol * 255);
    target_chan->entnum = entnum;
    target_chan->entchannel = entchannel;
    target_chan->generation = sound_generation;
    SND_Spatialize(target_chan);

    if (target_chan->leftvol || target_chan->rightvol) {
        target_chan->sfx = sfx;
        target_chan->pos = 0;
        target_chan->end = paintedtime + sfx->cache.effectiveLength;
    }
}

void S_StaticSound(sfx_t *sfx, vec3_t origin, float vol, float attenuation)
{
    if (!snd_initialized || !sfx || total_channels == MAX_CHANNELS ||
        sfx->cache.loopStart == -1) return;

    channel_t *ss = &channels[total_channels++];
    memset(ss, 0, sizeof(*ss));
    ss->sfx = sfx;
    VectorCopy(origin, ss->origin);
    ss->master_vol = (int)vol;
    ss->dist_mult = (attenuation / 64) / sound_nominal_clip_dist;
    ss->end = paintedtime + sfx->cache.effectiveLength;
    ss->generation = sound_generation;
    SND_Spatialize(ss);
}

void S_LocalSound(char *name)
{
    sfx_t *sfx = FindSfxName(name);
    if (sfx) S_StartSound(cl.viewentity, -1, sfx, vec3_origin, 1, 1);
}

void S_StopSound(int entnum, int entchannel)
{
    if (!snd_initialized) return;
    for (int i = 0; i < MAX_CHANNELS; ++i)
        if (channels[i].entnum == entnum && channels[i].entchannel == entchannel)
            channels[i].sfx = NULL;
}

sfx_t *S_PrecacheSound(char *name) { return FindSfxName(name); }
void S_TouchSound(char *name) { S_PrecacheSound(name); }

void S_Update(vec3_t origin, vec3_t forward, vec3_t right, vec3_t up)
{
    if (!snd_initialized) return;

    VectorCopy(origin, listener_origin);
    VectorCopy(forward, listener_forward);
    VectorCopy(right, listener_right);
    VectorCopy(up, listener_up);

    int soundtime_now = (int)total_samples_submitted;

    UpdateAmbientSounds();
    for (int i = NUM_AMBIENTS; i < total_channels; ++i) {
        if (!channels[i].sfx) continue;
        if (channels[i].end <= soundtime_now) {
            channels[i].sfx = NULL;
            continue;
        }
        SND_Spatialize(&channels[i]);
    }
}

void S_StopAllSounds(qboolean clear)
{
    if (!snd_initialized) return;
    total_channels = MAX_DYNAMIC_CHANNELS + NUM_AMBIENTS;
    memset(channels, 0, MAX_CHANNELS * sizeof(channel_t));
    sound_generation++;
    paintedtime = 0;
    total_samples_submitted = 0;
}

void S_ClearBuffer(void) {}
void S_BeginPrecaching(void)
{
    num_sfx = 0;
    memset(known_sfx, 0, sizeof(known_sfx));
    sound_generation++;
}
void S_EndPrecaching(void) {}
void S_ClearPrecache(void) {}
void S_ExtraUpdate(void) {}

void S_PrecacheAmbients(void)
{
    ambient_sfx[AMBIENT_WATER] = S_PrecacheSound("ambience/water1.wav");
    ambient_sfx[AMBIENT_SKY] = S_PrecacheSound("ambience/wind2.wav");
}
