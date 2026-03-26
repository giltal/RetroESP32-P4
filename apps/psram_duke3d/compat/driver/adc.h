/* Stub for PSRAM app build — no direct ADC in papp */
#ifndef PAPP_COMPAT_DRIVER_ADC_H
#define PAPP_COMPAT_DRIVER_ADC_H

typedef int adc1_channel_t;
typedef int adc_atten_t;
typedef int adc_bits_width_t;

#define ADC1_CHANNEL_0  0
#define ADC1_CHANNEL_3  3
#define ADC_ATTEN_DB_11 3
#define ADC_WIDTH_BIT_12 3

static inline void adc1_config_width(adc_bits_width_t w) { (void)w; }
static inline void adc1_config_channel_atten(adc1_channel_t ch, adc_atten_t atten) { (void)ch; (void)atten; }
static inline int adc1_get_raw(adc1_channel_t ch) { (void)ch; return 2048; /* center */ }

#endif
