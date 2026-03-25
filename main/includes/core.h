/*
 * RetroESP32-P4 — Core includes
 *
 * Adapted from RetroESP32 Odroid Go for the ESP32-P4 platform.
 * Uses the odroid compatibility component for hardware abstraction.
 */

/*
  C Standard
*/
#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <math.h>
#include <unistd.h>
#include <ctype.h>
#include <sys/stat.h>

/*
  FreeRTOS
*/
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/*
  NVS
*/
#include "nvs_flash.h"

/*
  SD Card
*/
#include "sdmmc_cmd.h"

/*
  ESP System
*/
#include "esp_heap_caps.h"
#include "esp_system.h"
#include "esp_event.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "esp_partition.h"

/*
  Drivers
*/
#include "driver/gpio.h"
#include "driver/ledc.h"

/*
  Odroid-compatible APIs (ESP32-P4 implementation)
*/
#include "odroid_settings.h"
#include "odroid_system.h"
#include "odroid_sdcard.h"
#include "odroid_display.h"
#include "odroid_input.h"
#include "odroid_audio.h"

/*
  Sprites
*/
#include "../sprites/battery.h"
#include "../sprites/brightness.h"
#include "../sprites/characters.h"
#include "../sprites/folder.h"
#include "../sprites/icons.h"
#include "../sprites/logo.h"
#include "../sprites/logo3d.h"
#include "../sprites/media.h"
#include "../sprites/speaker.h"
#include "../sprites/toggle.h"
#include "../sprites/systems.h"

/*
  Emulators
*/
#include "gnuboy_run.h"
#include "nofrendo_run.h"
#include "smsplus_run.h"
#include "prosystem_run.h"
#include "spectrum_run.h"
#include "stella_run.h"
#include "handy_run.h"
