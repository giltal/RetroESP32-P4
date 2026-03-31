/*
 * RetroESP32-P4 Launcher — Core includes
 *
 * Same as the original core.h but WITHOUT emulator headers.
 * Emulators run as separate OTA apps.
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
#include "esp_timer.h"
#include "esp_vfs_fat.h"
#include "esp_partition.h"
#include "esp_ota_ops.h"

/*
  Drivers
*/
#include "driver/gpio.h"
#include "driver/ledc.h"

/*
  Serial file upload (USB Serial JTAG)
*/
#include "serial_upload.h"

/*
  Odroid-compatible APIs (ESP32-P4 implementation)
*/
#include "odroid_settings.h"
#include "odroid_system.h"
#include "odroid_sdcard.h"
#include "odroid_display.h"
#include "odroid_input.h"
#include "odroid_audio.h"
#include "audio.h"

/*
  Touch Panel
*/
#include "gt911_touch.h"

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
#include "../sprites/genesis_icon.h"
#include "../sprites/font8x16.h"

/*
  PNG aux (image loading from SD card)
*/
#include "pngAux.h"

/*
  PPA engine + LCD (for native-size PNG splash)
*/
#include "ppa_engine.h"
#include "st7701_lcd.h"

/*
  PSRAM App Loader (load & execute apps from SD card via PSRAM XIP)
*/
#include "psram_app.h"

/* No emulator headers — emulators run as separate OTA apps */
