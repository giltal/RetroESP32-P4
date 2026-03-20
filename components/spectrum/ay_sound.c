/*
 * AY-3-8912 PSG emulation for ZX Spectrum 128K sound
 *
 * Registers:
 *   R0/R1  - Channel A tone period (12-bit)
 *   R2/R3  - Channel B tone period
 *   R4/R5  - Channel C tone period
 *   R6     - Noise period (5-bit)
 *   R7     - Mixer: tone enable (bits 0-2, active low), noise enable (bits 3-5, active low)
 *   R8-R10 - Channel volume (4-bit) or envelope mode (bit 4)
 *   R11/R12- Envelope period (16-bit)
 *   R13    - Envelope shape
 */

#include "ay_sound.h"
#include <string.h>

AYChip ay_chip;

/* Logarithmic volume table (DAC output levels, 0..15 → amplitude) */
static const uint16_t vol_table[16] = {
    0, 51, 73, 103, 146, 207, 292, 414,
    585, 828, 1171, 1656, 2342, 3313, 4685, 6626
};

void ay_init(int sample_rate)
{
    memset(&ay_chip, 0, sizeof(ay_chip));
    ay_chip.sample_rate = sample_rate;
    ay_chip.noise_lfsr = 1;  /* must be non-zero */
    ay_chip.regs[7] = 0xFF;  /* all channels disabled by default */
}

void ay_reset(void)
{
    int sr = ay_chip.sample_rate;
    ay_init(sr);
}

void ay_select_reg(uint8_t reg)
{
    ay_chip.selected_reg = reg & 0x0F;
}

void ay_write_data(uint8_t val)
{
    uint8_t reg = ay_chip.selected_reg;
    if (reg >= AY_NUM_REGS) return;

    /* Mask register values to valid bit widths */
    switch (reg) {
        case 1: case 3: case 5:
            val &= 0x0F; break;   /* tone coarse: 4 bits */
        case 6:
            val &= 0x1F; break;   /* noise period: 5 bits */
        case 8: case 9: case 10:
            val &= 0x1F; break;   /* volume: 5 bits (bit 4 = envelope) */
        case 13:
            val &= 0x0F;
            /* Writing to envelope shape restarts the envelope */
            ay_chip.env_step = 0;
            ay_chip.env_cnt = 0;
            ay_chip.env_holding = 0;
            break;
    }
    ay_chip.regs[reg] = val;
}

uint8_t ay_read_data(void)
{
    if (ay_chip.selected_reg >= AY_NUM_REGS) return 0xFF;
    return ay_chip.regs[ay_chip.selected_reg];
}

/*
 * Envelope shape lookup.
 * Each shape has 32 steps (two half-cycles of 16 steps).
 * Returns volume 0-15 for a given step (0-31) and shape (0-15).
 */
static uint8_t env_volume(uint8_t shape, int step)
{
    /* Normalize step to 0..31 range for non-holding shapes */
    int s = step & 31;
    int first_half = (s < 16);
    int pos = s & 15;

    uint8_t attack  = (shape & 0x04) ? 1 : 0;  /* bit 2: attack */
    uint8_t cont    = (shape & 0x08) ? 1 : 0;  /* bit 3: continue */
    uint8_t alt     = (shape & 0x02) ? 1 : 0;  /* bit 1: alternate */
    uint8_t hold    = (shape & 0x01) ? 1 : 0;  /* bit 0: hold */

    if (first_half) {
        /* First cycle: attack=ramp up, !attack=ramp down */
        return attack ? pos : (15 - pos);
    }

    if (!cont) {
        /* Not continue: output 0 after first cycle */
        return 0;
    }

    if (hold) {
        /* Hold: stay at final value */
        if (alt ^ attack)
            return 15;
        else
            return 0;
    }

    if (alt) {
        /* Alternate: reverse direction */
        return attack ? (15 - pos) : pos;
    }

    /* Continue without hold or alt: repeat same shape */
    return attack ? pos : (15 - pos);
}

void ay_render(int16_t *buf, int num_samples)
{
    AYChip *ay = &ay_chip;
    int sr = ay->sample_rate;
    if (sr == 0) sr = 15600;

    /* Precompute tone periods */
    int tone_period[3];
    for (int ch = 0; ch < 3; ch++) {
        tone_period[ch] = ay->regs[ch * 2] | ((ay->regs[ch * 2 + 1] & 0x0F) << 8);
        if (tone_period[ch] == 0) tone_period[ch] = 1;
    }

    int noise_period = ay->regs[6] & 0x1F;
    if (noise_period == 0) noise_period = 1;

    int env_period = ay->regs[11] | (ay->regs[12] << 8);
    if (env_period == 0) env_period = 1;

    uint8_t mixer = ay->regs[7];
    uint8_t shape = ay->regs[13];

    /*
     * Tone counters run at AY_CLOCK/16, noise at AY_CLOCK/16,
     * envelope at AY_CLOCK/256.
     *
     * Use Bresenham-style accumulation:
     *   counter += AY_CLOCK_DIV_N
     *   when counter >= period * sample_rate → toggle, subtract
     */
    #define AY_CLOCK_DIV16  (AY_CLOCK / 16)   /* 110837 */
    #define AY_CLOCK_DIV256 (AY_CLOCK / 256)   /* 6927 */

    for (int s = 0; s < num_samples; s++) {

        /* --- Advance tone generators --- */
        for (int ch = 0; ch < 3; ch++) {
            ay->tone_cnt[ch] += AY_CLOCK_DIV16;
            int threshold = tone_period[ch] * sr;
            while (ay->tone_cnt[ch] >= threshold) {
                ay->tone_cnt[ch] -= threshold;
                ay->tone_out[ch] ^= 1;
            }
        }

        /* --- Advance noise generator --- */
        ay->noise_cnt += AY_CLOCK_DIV16;
        {
            int threshold = noise_period * sr;
            while (ay->noise_cnt >= threshold) {
                ay->noise_cnt -= threshold;
                /* 17-bit LFSR: taps at bits 0 and 3 */
                uint32_t bit = ((ay->noise_lfsr) ^ (ay->noise_lfsr >> 3)) & 1;
                ay->noise_lfsr = (ay->noise_lfsr >> 1) | (bit << 16);
                ay->noise_out = ay->noise_lfsr & 1;
            }
        }

        /* --- Advance envelope generator --- */
        if (!ay->env_holding) {
            ay->env_cnt += AY_CLOCK_DIV256;
            int threshold = env_period * sr;
            while (ay->env_cnt >= threshold) {
                ay->env_cnt -= threshold;
                ay->env_step++;
                if (ay->env_step >= 32) {
                    /* Check if shape repeats or holds */
                    uint8_t cont = (shape & 0x08);
                    uint8_t hold_bit = (shape & 0x01);
                    if (!cont || hold_bit) {
                        ay->env_holding = 1;
                        ay->env_step = 31;
                    } else {
                        ay->env_step = 0;
                    }
                }
            }
            ay->env_volume = env_volume(shape, ay->env_step);
        }

        /* --- Mix channels --- */
        int32_t total = 0;
        for (int ch = 0; ch < 3; ch++) {
            uint8_t tone_en  = !((mixer >> ch) & 1);       /* active low */
            uint8_t noise_en = !((mixer >> (ch + 3)) & 1); /* active low */

            uint8_t tone_val  = tone_en  ? ay->tone_out[ch] : 1;
            uint8_t noise_val = noise_en ? ay->noise_out    : 1;

            if (tone_val & noise_val) {
                uint8_t vol_reg = ay->regs[8 + ch];
                int vol;
                if (vol_reg & 0x10) {
                    vol = ay->env_volume;
                } else {
                    vol = vol_reg & 0x0F;
                }
                total += vol_table[vol];
            }
        }

        /* Scale: max total = 3 * 6626 = 19878. Map to signed 16-bit range. */
        int16_t sample = (int16_t)(total * 32767 / 19878);
        /* Reduce volume slightly so it doesn't overpower beeper */
        sample = (sample * 3) >> 2;  /* 75% */
        buf[s] = sample;
    }
}
