/*
 * AY-3-8912 PSG emulation for ZX Spectrum 128K sound
 */
#ifndef AY_SOUND_H
#define AY_SOUND_H

#include <stdint.h>

#define AY_NUM_REGS 16
#define AY_CLOCK    1773400  /* Spectrum 128K AY clock = CPU/2 */

typedef struct {
    uint8_t  regs[AY_NUM_REGS];
    uint8_t  selected_reg;

    /* Tone generators (3 channels) */
    int32_t  tone_cnt[3];
    uint8_t  tone_out[3];     /* current square-wave output: 0 or 1 */

    /* Noise generator */
    int32_t  noise_cnt;
    uint32_t noise_lfsr;      /* 17-bit LFSR */
    uint8_t  noise_out;

    /* Envelope generator */
    int32_t  env_cnt;
    int8_t   env_step;        /* current step within shape (0..31) */
    uint8_t  env_volume;      /* current envelope volume 0..15 */
    uint8_t  env_holding;     /* 1 = envelope in hold state */

    /* Rendering state */
    int      sample_rate;
} AYChip;

extern AYChip ay_chip;

void    ay_init(int sample_rate);
void    ay_reset(void);
void    ay_select_reg(uint8_t reg);
void    ay_write_data(uint8_t val);
uint8_t ay_read_data(void);
void    ay_render(int16_t *buf, int num_samples);

#endif /* AY_SOUND_H */
