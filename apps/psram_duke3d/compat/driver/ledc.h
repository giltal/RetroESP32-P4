/* Stub for PSRAM app build — no LEDC in papp */
#ifndef PAPP_COMPAT_DRIVER_LEDC_H
#define PAPP_COMPAT_DRIVER_LEDC_H

typedef int ledc_timer_config_t;
typedef int ledc_channel_config_t;

static inline int ledc_timer_config(void *cfg) { (void)cfg; return 0; }
static inline int ledc_channel_config(void *cfg) { (void)cfg; return 0; }
static inline int ledc_set_duty(int mode, int ch, int duty) { (void)mode; (void)ch; (void)duty; return 0; }
static inline int ledc_update_duty(int mode, int ch) { (void)mode; (void)ch; return 0; }

#endif
