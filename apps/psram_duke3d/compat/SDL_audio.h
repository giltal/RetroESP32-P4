/*
 * SDL_audio.h compat header for Duke3D PSRAM app.
 * Audio implemented in papp_sdl_audio.c via the papp service table.
 */
#ifndef SDL_audio_h_
#define SDL_audio_h_

#include "SDL_stdinc.h"

#define SAMPLECOUNT  512
#define SAMPLERATE   22050
#define SAMPLESIZE   2     /* 16-bit */

typedef struct {
    int needed;
    Uint16 src_format;
    Uint16 dest_format;
    double rate_incr;
    Uint8 *buf;
    int len;
    int len_cvt;
    int len_mult;
    double len_ratio;
    int filter_index;
} SDL_AudioCVT;

typedef struct {
    int freq;
    Uint16 format;
    Uint8 channels;
    Uint8 silence;
    Uint16 samples;
    Uint32 size;
    void (*callback)(void *userdata, Uint8 *stream, int len);
    void *userdata;
} SDL_AudioSpec;

int SDL_OpenAudio(SDL_AudioSpec *desired, SDL_AudioSpec *obtained);
void SDL_PauseAudio(int pause_on);
void SDL_CloseAudio(void);
int SDL_BuildAudioCVT(SDL_AudioCVT *cvt, Uint16 src_format, Uint8 src_channels,
                      int src_rate, Uint16 dst_format, Uint8 dst_channels, int dst_rate);
int SDL_ConvertAudio(SDL_AudioCVT *cvt);
void SDL_LockAudio(void);
void SDL_UnlockAudio(void);

#endif /* SDL_audio_h_ */
