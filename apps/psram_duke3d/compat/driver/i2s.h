/* Stub for PSRAM app build — no direct I2S in papp */
#ifndef PAPP_COMPAT_DRIVER_I2S_H
#define PAPP_COMPAT_DRIVER_I2S_H

#include <stddef.h>
#include <stdint.h>

#define I2S_NUM_0           0
#define I2S_MODE_MASTER     0x01
#define I2S_MODE_TX         0x04
#define I2S_MODE_DAC_BUILT_IN 0x10
#define I2S_CHANNEL_FMT_RIGHT_LEFT 0
#define I2S_COMM_FORMAT_I2S_MSB    0
#define I2S_BITS_PER_SAMPLE_16BIT 16
#define I2S_CHANNEL_STEREO 2
#define I2S_DAC_CHANNEL_BOTH_EN 0

typedef struct {
    int mode;
    int sample_rate;
    int bits_per_sample;
    int channel_format;
    int communication_format;
    int dma_buf_count;
    int dma_buf_len;
    int intr_alloc_flags;
    int use_apll;
} i2s_config_t;

#define ESP_ERROR_CHECK(x) (void)(x)

static inline int i2s_driver_install(int num, const i2s_config_t *cfg, int qsz, void *q) {
    (void)num; (void)cfg; (void)qsz; (void)q; return 0;
}
static inline int i2s_set_pin(int num, void *cfg) { (void)num; (void)cfg; return 0; }
static inline int i2s_set_dac_mode(int mode) { (void)mode; return 0; }
static inline int i2s_driver_uninstall(int num) { (void)num; return 0; }
static inline int i2s_write(int num, const void *src, size_t sz, size_t *out, int ticks) {
    (void)num; (void)src; (void)sz; (void)ticks;
    if (out) *out = sz;
    return 0;
}

#endif
