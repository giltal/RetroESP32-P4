/* Stub for PSRAM app build — no direct GPIO in papp */
#ifndef PAPP_COMPAT_DRIVER_GPIO_H
#define PAPP_COMPAT_DRIVER_GPIO_H

#include <stdint.h>

typedef int gpio_num_t;
typedef int gpio_config_t;
typedef int gpio_int_type_t;
typedef int gpio_isr_t;
typedef int gpio_mode_t;

#define GPIO_MODE_INPUT  0
#define GPIO_MODE_OUTPUT 1
#define GPIO_INTR_NEGEDGE 0
#define GPIO_INTR_DISABLE 0
#define ESP_INTR_FLAG_LEVEL1 (1<<1)

static inline int gpio_get_level(gpio_num_t pin) { (void)pin; return 0; }
static inline int gpio_set_level(gpio_num_t pin, uint32_t lvl) { (void)pin; (void)lvl; return 0; }
static inline int gpio_install_isr_service(int flags) { (void)flags; return 0; }
static inline int gpio_isr_handler_add(gpio_num_t pin, void *fn, void *arg) { (void)pin; (void)fn; (void)arg; return 0; }

#endif
